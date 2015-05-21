/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2015 Synergy Si Inc.
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "server/ClientProxy1_6.h"

#include "server/Server.h"
#include "synergy/ProtocolUtil.h"
#include "synergy/StreamChunker.h"
#include "synergy/ClipboardChunk.h"
#include "io/IStream.h"
#include "base/TMethodEventJob.h"
#include "base/Log.h"

//
// ClientProxy1_6
//

ClientProxy1_6::ClientProxy1_6(const String& name, synergy::IStream* stream, Server* server, IEventQueue* events) :
	ClientProxy1_5(name, stream, server, events),
	m_events(events)
{
	m_events->adoptHandler(m_events->forClipboard().clipboardSending(),
								this,
								new TMethodEventJob<ClientProxy1_6>(this,
									&ClientProxy1_6::handleClipboardSendingEvent));
}

ClientProxy1_6::~ClientProxy1_6()
{
}

void
ClientProxy1_6::setClipboard(ClipboardID id, const IClipboard* clipboard)
{
	// ignore if this clipboard is already clean
	if (m_clipboard[id].m_dirty) {
		// this clipboard is now clean
		m_clipboard[id].m_dirty = false;
		Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);

		String data = m_clipboard[id].m_clipboard.marshall();

		size_t size = data.size();
		LOG((CLOG_DEBUG "sending clipboard %d to \"%s\"", id, getName().c_str()));

		StreamChunker::sendClipboard(data, size, id, 0, m_events, this);

		LOG((CLOG_DEBUG "sent clipboard size=%d", size));
	}
}

void
ClientProxy1_6::handleClipboardSendingEvent(const Event& event, void*)
{
	void* data = event.getData();
	ClipboardChunk* clipboardData = reinterpret_cast<ClipboardChunk*>(data);

	LOG((CLOG_DEBUG1 "sending clipboard chunk"));

	char* chunk = clipboardData->m_chunk;
	ClipboardID id = chunk[0];
	UInt32* seq = reinterpret_cast<UInt32*>(&chunk[1]);
	UInt32 sequence = *seq;
	UInt8 mark = chunk[5];
	String dataChunk(&chunk[6], clipboardData->m_dataSize);

	switch (mark) {
	case kDataStart:
		LOG((CLOG_DEBUG2 "file sending start: size=%s", dataChunk.c_str()));
		break;

	case kDataChunk:
		LOG((CLOG_DEBUG2 "file chunk sending: size=%i", dataChunk.size()));
		break;

	case kDataEnd:
		LOG((CLOG_DEBUG2 "file sending finished"));
		break;
	}

	ProtocolUtil::writef(getStream(), kMsgDClipboard, id, sequence, mark, &dataChunk);
}

bool
ClientProxy1_6::recvClipboard()
{
	// parse message
	static String dataCached;
	ClipboardID id;
	UInt32 seq;

	int r = ClipboardChunk::assemble(getStream(), dataCached, id, seq);

	if (r == kFinish) {
		LOG((CLOG_DEBUG "received client \"%s\" clipboard %d seqnum=%d, size=%d", getName().c_str(), id, seq, dataCached.size()));
		// save clipboard
		m_clipboard[id].m_clipboard.unmarshall(dataCached, 0);
		m_clipboard[id].m_sequenceNumber = seq;
		
		// notify
		ClipboardInfo* info = new ClipboardInfo;
		info->m_id = id;
		info->m_sequenceNumber = seq;
		m_events->addEvent(Event(m_events->forClipboard().clipboardChanged(),
								 getEventTarget(), info));
	}

	return true;
}

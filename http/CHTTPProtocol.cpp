#include "CHTTPProtocol.h"
#include "CLog.h"
#include "XHTTP.h"
#include "IInputStream.h"
#include "IOutputStream.h"
#include <ctype.h>
#include <locale.h>
#include <time.h>
#include <algorithm>
#include <sstream>

//
// CHTTPUtil::CaselessCmp
//

inline
bool					CHTTPUtil::CaselessCmp::cmpEqual(
								const CString::value_type& a,
								const CString::value_type& b)
{
	// FIXME -- use std::tolower
	return tolower(a) == tolower(b);
}

inline
bool					CHTTPUtil::CaselessCmp::cmpLess(
								const CString::value_type& a,
								const CString::value_type& b)
{
	// FIXME -- use std::tolower
	return tolower(a) < tolower(b);
}

bool					CHTTPUtil::CaselessCmp::less(
								const CString& a,
								const CString& b)
{
	return std::lexicographical_compare(
								a.begin(), a.end(),
								b.begin(), b.end(),
								&CHTTPUtil::CaselessCmp::cmpLess);
}

bool					CHTTPUtil::CaselessCmp::equal(
								const CString& a,
								const CString& b)
{
	return !(less(a, b) || less(b, a));
}

bool					CHTTPUtil::CaselessCmp::operator()(
								const CString& a,
								const CString& b) const
{
	return less(a, b);
}


//
// CHTTPProtocol
//
					
CHTTPRequest*		CHTTPProtocol::readRequest(IInputStream* stream)
{
	CString scratch;

	// parse request line by line
	CHTTPRequest* request = new CHTTPRequest;
	try {
		CString line;

		// read request line.  accept and discard leading empty lines.
		do {
			line = readLine(stream, scratch);
		} while (line.empty());

		// parse request line:  <method> <uri> <version>
		{
			std::istringstream s(line);
			s.exceptions(std::ios::goodbit);
			CString version;
			s >> request->m_method >> request->m_uri >> version;
			if (!s || request->m_uri.empty() || version.find("HTTP/") != 0) {
				log((CLOG_DEBUG1 "failed to parse HTTP request line: %s", line.c_str()));
				throw XHTTP(400);
			}

			// parse version
			char dot;
			s.str(version);
			s.ignore(5);
			s >> request->m_majorVersion;
			s.get(dot);
			s >> request->m_minorVersion;
			if (!s || dot != '.') {
				log((CLOG_DEBUG1 "failed to parse HTTP request line: %s", line.c_str()));
				throw XHTTP(400);
			}
		}
		if (!isValidToken(request->m_method)) {
			log((CLOG_DEBUG1 "invalid HTTP method: %s", line.c_str()));
			throw XHTTP(400);
		}
		if (request->m_majorVersion < 1 || request->m_minorVersion < 0) {
			log((CLOG_DEBUG1 "invalid HTTP version: %s", line.c_str()));
			throw XHTTP(400);
		}

		// parse headers
		readHeaders(stream, request, false, scratch);

		// HTTP/1.1 requests must have a Host header
		if (request->m_majorVersion > 1 ||
			(request->m_majorVersion == 1 && request->m_minorVersion >= 1)) {
			if (request->m_headerIndexByName.count("Host") == 0) {
				log((CLOG_DEBUG1 "Host header missing"));
				throw XHTTP(400);
			}
		}

		// some methods may not have a body.  ensure that the headers
		// that indicate the body length do not exist for those methods
		// and do exist for others.
		if ((request->m_headerIndexByName.count("Transfer-Encoding") == 0 &&
			 request->m_headerIndexByName.count("Content-Length") == 0) !=
			(request->m_method == "GET" ||
			 request->m_method == "HEAD")) {
			log((CLOG_DEBUG1 "HTTP method (%s)/body mismatch", request->m_method.c_str()));
			throw XHTTP(400);
		}

		// prepare to read the body.  the length of the body is
		// determined using, in order:
		//   1. Transfer-Encoding indicates a "chunked" transfer
		//   2. Content-Length is present
		// Content-Length is ignored for "chunked" transfers.
		CHTTPRequest::CHeaderMap::iterator index = request->
								m_headerIndexByName.find("Transfer-Encoding");
		if (index != request->m_headerIndexByName.end()) {
			// we only understand "chunked" encodings
			if (!CHTTPUtil::CaselessCmp::equal(
								request->m_headers[index->second], "chunked")) {
				log((CLOG_DEBUG1 "unsupported Transfer-Encoding %s", request->m_headers[index->second].c_str()));
				throw XHTTP(501);
			}

			// chunked encoding
			UInt32 oldSize;
			do {
				oldSize = request->m_body.size();
				request->m_body += readChunk(stream, scratch);
			} while (request->m_body.size() != oldSize);

			// read footer
			readHeaders(stream, request, true, scratch);

			// remove "chunked" from Transfer-Encoding and set the
			// Content-Length.
			// FIXME
			// FIXME -- note that just deleting Transfer-Encoding will
			// mess up indices in m_headerIndexByName, and replacing
			// it with Content-Length could lead to two of those.
		}
		else if ((index = request->m_headerIndexByName.
								find("Content-Length")) !=
								request->m_headerIndexByName.end()) {
			// FIXME -- check for overly-long requests

			// parse content-length
			UInt32 length;
			{
				std::istringstream s(request->m_headers[index->second]);
				s.exceptions(std::ios::goodbit);
				s >> length;
				if (!s) {
					log((CLOG_DEBUG1 "cannot parse Content-Length", request->m_headers[index->second].c_str()));
					throw XHTTP(400);
				}
			}

			// use content length
			request->m_body = readBlock(stream, length, scratch);
			if (request->m_body.size() != length) {
				// length must match size of body
				log((CLOG_DEBUG1 "Content-Length/actual length mismatch (%d vs %d)", length, request->m_body.size()));
				throw XHTTP(400);
			}
		}
	}
	catch (...) {
		delete request;
		throw;
	}

	return request;
}

void					CHTTPProtocol::reply(
								IOutputStream* stream,
								CHTTPReply& reply)
{
	// suppress body for certain replies
	bool hasBody = true;
	if ((reply.m_status / 100) == 1 ||
		reply.m_status == 204 ||
		reply.m_status == 304) {
		hasBody = false;
	}

	// adjust headers
	for (CHTTPReply::CHeaderList::iterator
								index = reply.m_headers.begin();
								index != reply.m_headers.end(); ) {
		const CString& header = index->first;

		// remove certain headers
		if (CHTTPUtil::CaselessCmp::equal(header, "Content-Length") ||
			CHTTPUtil::CaselessCmp::equal(header, "Date") ||
			CHTTPUtil::CaselessCmp::equal(header, "Transfer-Encoding")) {
			// FIXME -- Transfer-Encoding should be left as-is if
			// not "chunked" and if the version is 1.1 or up.
			index = reply.m_headers.erase(index);
		}

		// keep as-is
		else {
			++index;
		}
	}

	// write reply header
	ostringstream s;
	s << "HTTP/" << reply.m_majorVersion << "." <<
					reply.m_minorVersion << " " <<
					reply.m_status << " " <<
					reply.m_reason << "\r\n";

	// get date
	// FIXME -- should use C++ locale stuff but VC++ time_put is broken.
	// FIXME -- double check that VC++ is broken
	// FIXME -- should mutex gmtime() since the return value may not
	// be thread safe
	char date[30];
	{
		const char* oldLocale = setlocale(LC_TIME, "C");
		time_t t = time(NULL);
		struct tm* tm = gmtime(&t);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", tm);
		setlocale(LC_TIME, oldLocale);
	}

	// write headers
	s << "Date: " << date << "\r\n";
	for (CHTTPReply::CHeaderList::const_iterator
								index = reply.m_headers.begin();
								index != reply.m_headers.end(); ++index) {
		s << index->first << ": " << index->second << "\r\n";
	}
	if (hasBody) {
		s << "Content-Length: " << reply.m_body.size() << "\r\n";
	}
	s << "Connection: close\r\n";

	// write end of headers
	s << "\r\n";

	// write to stream
	stream->write(s.str().data(), s.str().size());

	// write body.  replies to HEAD method never have a body (though
	// they do have the Content-Length header).
	if (hasBody && reply.m_method != "HEAD") {
		stream->write(reply.m_body.data(), reply.m_body.size());
	}
}

bool					CHTTPProtocol::parseFormData(
								const CHTTPRequest& request,
								CFormParts& parts)
{
	static const char formData[]    = "multipart/form-data";
	static const char boundary[]    = "boundary=";
	static const char disposition[] = "Content-Disposition:";
	static const char nameAttr[]    = "name=";
	static const char quote[]       = "\"";

	// find the Content-Type header
	CHTTPRequest::CHeaderMap::const_iterator contentTypeIndex =
							request.m_headerIndexByName.find("Content-Type");
	if (contentTypeIndex == request.m_headerIndexByName.end()) {
		// missing required Content-Type header
		return false;
	}
	const CString contentType = request.m_headers[contentTypeIndex->second];

	// parse type
	CString::const_iterator index = std::search(
								contentType.begin(), contentType.end(),
								formData, formData + sizeof(formData) - 1,
								CHTTPUtil::CaselessCmp::cmpEqual);
	if (index == contentType.end()) {
		// not form-data
		return false;
	}
	index += sizeof(formData) - 1;
	index = std::search(index, contentType.end(),
								boundary, boundary + sizeof(boundary) - 1,
								CHTTPUtil::CaselessCmp::cmpEqual);
	if (index == contentType.end()) {
		// no boundary
		return false;
	}
	CString delimiter = contentType.c_str() +
								(index - contentType.begin()) +
								sizeof(boundary) - 1;

	// find first delimiter
	const CString& body = request.m_body;
	CString::size_type partIndex = body.find(delimiter);
	if (partIndex == CString::npos) {
		return false;
	}

	// skip over it
	partIndex += delimiter.size();

	// prepend CRLF-- to delimiter
	delimiter = "\r\n--" + delimiter;

	// parse parts until there are no more
	for (;;) {
		// is it the last part?
		if (body.size() >= partIndex + 2 &&
			body[partIndex    ] == '-' &&
			body[partIndex + 1] == '-') {
			// found last part.  success if there's no trailing data.
			// FIXME -- check for trailing data (other than a single CRLF)
			return true;
		}

		// find the end of this part
		CString::size_type nextPart = body.find(delimiter, partIndex);
		if (nextPart == CString::npos) {
			// no terminator
			return false;
		}

		// find end of headers
		CString::size_type endOfHeaders = body.find("\r\n\r\n", partIndex);
		if (endOfHeaders == CString::npos || endOfHeaders > nextPart) {
			// bad part
			return false;
		}
		endOfHeaders += 2;

		// now find Content-Disposition
		index = std::search(body.begin() + partIndex,
								body.begin() + endOfHeaders,
								disposition,
								disposition + sizeof(disposition) - 1,
								CHTTPUtil::CaselessCmp::cmpEqual);
		if (index == contentType.begin() + endOfHeaders) {
			// bad part
			return false;
		}

		// find the name in the Content-Disposition
		CString::size_type endOfHeader = body.find("\r\n",
								index - body.begin());
		if (endOfHeader >= endOfHeaders) {
			// bad part
			return false;
		}
		index = std::search(index, body.begin() + endOfHeader,
								nameAttr, nameAttr + sizeof(nameAttr) - 1,
								CHTTPUtil::CaselessCmp::cmpEqual);
		if (index == body.begin() + endOfHeader) {
			// no name
			return false;
		}

		// extract the name
		CString name;
		index += sizeof(nameAttr) - 1;
		if (*index == quote[0]) {
			// quoted name
			++index;
			CString::size_type namePos = index - body.begin();
			index = std::search(index, body.begin() + endOfHeader,
								quote, quote + 1,
								CHTTPUtil::CaselessCmp::cmpEqual);
			if (index == body.begin() + endOfHeader) {
				// missing close quote
				return false;
			}
			name = body.substr(namePos, index - body.begin() - namePos);
		}
		else {
			// unquoted name
			name = body.substr(index - body.begin(),
								body.find_first_of(" \t\r\n"));
		}

		// save part.  add 2 to endOfHeaders to skip CRLF.
		parts.insert(std::make_pair(name, body.substr(endOfHeaders + 2,
									nextPart - (endOfHeaders + 2))));

		// move to next part
		partIndex = nextPart + delimiter.size();
	}

	// should've found the last delimiter inside the loop but we did not
	return false;	
}

CString					CHTTPProtocol::readLine(
								IInputStream* stream,
								CString& tmpBuffer)
{
	// read up to and including a CRLF from stream, using whatever
	// is in tmpBuffer as if it were at the head of the stream.

	for (;;) {
		// scan tmpBuffer for CRLF
		CString::size_type newline = tmpBuffer.find("\r\n");
		if (newline != CString::npos) {
			// copy line without the CRLF
			CString line = tmpBuffer.substr(0, newline);

			// discard line and CRLF from tmpBuffer
			tmpBuffer.erase(0, newline + 2);
			return line;
		}

		// read more from stream
		char buffer[4096];
		UInt32 n = stream->read(buffer, sizeof(buffer));
		if (n == 0) {
			// stream is empty.  return what's leftover.
			CString line = tmpBuffer;
			tmpBuffer.erase();
			return line;
		}

		// append stream data
		tmpBuffer.append(buffer, n);
	}
}

CString					CHTTPProtocol::readBlock(
								IInputStream* stream,
								UInt32 numBytes,
								CString& tmpBuffer)
{
	CString data;

	// read numBytes from stream, using whatever is in tmpBuffer as
	// if it were at the head of the stream.
	if (tmpBuffer.size() > 0) {
		// ignore stream if there's enough data in tmpBuffer
		if (tmpBuffer.size() >= numBytes) {
			data = tmpBuffer.substr(0, numBytes);
			tmpBuffer.erase(0, numBytes);
			return data;
		}

		// move everything out of tmpBuffer into data
		data = tmpBuffer;
		tmpBuffer.erase();
	}

	// account for bytes read so far
	assert(data.size() < numBytes);
	numBytes -= data.size();

	// read until we have all the requested data
	while (numBytes > 0) {
		// read max(4096, bytes_left) bytes into buffer
		char buffer[4096];
		UInt32 n = sizeof(buffer);
		if (n > numBytes) {
			n = numBytes;
		}
		n = stream->read(buffer, n);

		// if stream is empty then return what we've got so far
		if (n == 0) {
			break;
		}

		// append stream data
		data.append(buffer, n);
		numBytes -= n;
	}

	return data;
}

CString					CHTTPProtocol::readChunk(
								IInputStream* stream,
								CString& tmpBuffer)
{
	CString line;

	// get chunk header
	line = readLine(stream, tmpBuffer);

	// parse chunk size
	UInt32 size;
	{
		std::istringstream s(line);
		s.exceptions(std::ios::goodbit);
		s >> std::hex >> size;
		if (!s) {
			log((CLOG_DEBUG1 "cannot parse chunk size", line.c_str()));
			throw XHTTP(400);
		}
	}
	if (size == 0) {
		return CString();
	}

	// read size bytes
	// FIXME -- check for overly-long requests
	CString data = readBlock(stream, size, tmpBuffer);
	if (data.size() != size) {
		log((CLOG_DEBUG1 "expected/actual chunk size mismatch", size, data.size()));
		throw XHTTP(400);
	}

	// read an discard CRLF
	line = readLine(stream, tmpBuffer);
	if (!line.empty()) {
		log((CLOG_DEBUG1 "missing CRLF after chunk"));
		throw XHTTP(400);
	}

	return data;
}

void					CHTTPProtocol::readHeaders(
								IInputStream* stream,
								CHTTPRequest* request,
								bool isFooter,
								CString& tmpBuffer)
{
	// parse headers.  done with headers when we get a blank line.
	CString line = readLine(stream, tmpBuffer);
	while (!line.empty()) {
		// if line starts with space or tab then append it to the
		// previous header.  if there is no previous header then
		// throw.
		if (line[0] == ' ' || line[0] == '\t') {
			if (request->m_headers.size() == 0) {
				log((CLOG_DEBUG1 "first header is a continuation"));
				throw XHTTP(400);
			}
			request->m_headers.back() += ",";
			request->m_headers.back() == line;
		}

		// line should have the form:  <name>:[<value>]
		else {
			// parse
			CString name, value;
			std::istringstream s(line);
			s.exceptions(std::ios::goodbit);
			std::getline(s, name, ':');
			if (!s || !isValidToken(name)) {
				log((CLOG_DEBUG1 "invalid header: %s", line.c_str()));
				throw XHTTP(400);
			}
			std::getline(s, value);

			// check validity of name
			if (isFooter) {
				// FIXME -- only certain names are allowed in footers
			}

			// check if we've seen this header before
			CHTTPRequest::CHeaderMap::iterator index =
								request->m_headerIndexByName.find(name);
			if (index == request->m_headerIndexByName.end()) {
				// it's a new header
				request->m_headerIndexByName.insert(std::make_pair(name,
											request->m_headers.size()));
				request->m_headers.push_back(value);
			}
			else {
				// it's an existing header.  append value to previous
				// header, separated by a comma.
				request->m_headers[index->second] += ',';
				request->m_headers[index->second] += value;
			}
		}

		// next header
		line = readLine(stream, tmpBuffer);

		// FIXME -- should check for overly-long requests
	}
}

bool					CHTTPProtocol::isValidToken(const CString& token)
{
	return (token.find("()<>@,;:\\\"/[]?={} "
					"\0\1\2\3\4\5\6\7"
					"\10\11\12\13\14\15\16\17"
					"\20\21\22\23\24\25\26\27"
					"\30\31\32\33\34\35\36\37\177") == CString::npos);
}
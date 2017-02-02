/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "ESPAsyncWebServer.h"
#include "WebResponseImpl.h"
#include "cbuf.h"

/*
 * Abstract Response
 * */
const char* AsyncWebServerResponse::_responseCodeToString(int code) {
  switch (code) {
    case 100: return String(F("Continue")).c_str();
    case 101: return String(F("Switching Protocols")).c_str();
    case 200: return String(F("OK")).c_str();
    case 201: return String(F("Created")).c_str();
    case 202: return String(F("Accepted")).c_str();
    case 203: return String(F("Non-Authoritative Information")).c_str();
    case 204: return String(F("No Content")).c_str();
    case 205: return String(F("Reset Content")).c_str();
    case 206: return String(F("Partial Content")).c_str();
    case 300: return String(F("Multiple Choices")).c_str();
    case 301: return String(F("Moved Permanently")).c_str();
    case 302: return String(F("Found")).c_str();
    case 303: return String(F("See Other")).c_str();
    case 304: return String(F("Not Modified")).c_str();
    case 305: return String(F("Use Proxy")).c_str();
    case 307: return String(F("Temporary Redirect")).c_str();
    case 400: return String(F("Bad Request")).c_str();
    case 401: return String(F("Unauthorized")).c_str();
    case 402: return String(F("Payment Required")).c_str();
    case 403: return String(F("Forbidden")).c_str();
    case 404: return String(F("Not Found")).c_str();
    case 405: return String(F("Method Not Allowed")).c_str();
    case 406: return String(F("Not Acceptable")).c_str();
    case 407: return String(F("Proxy Authentication Required")).c_str();
    case 408: return String(F("Request Time-out")).c_str();
    case 409: return String(F("Conflict")).c_str();
    case 410: return String(F("Gone")).c_str();
    case 411: return String(F("Length Required")).c_str();
    case 412: return String(F("Precondition Failed")).c_str();
    case 413: return String(F("Request Entity Too Large")).c_str();
    case 414: return String(F("Request-URI Too Large")).c_str();
    case 415: return String(F("Unsupported Media Type")).c_str();
    case 416: return String(F("Requested range not satisfiable")).c_str();
    case 417: return String(F("Expectation Failed")).c_str();
    case 500: return String(F("Internal Server Error")).c_str();
    case 501: return String(F("Not Implemented")).c_str();
    case 502: return String(F("Bad Gateway")).c_str();
    case 503: return String(F("Service Unavailable")).c_str();
    case 504: return String(F("Gateway Time-out")).c_str();
    case 505: return String(F("HTTP Version not supported")).c_str();
    default:  return "";
  }
}

AsyncWebServerResponse::AsyncWebServerResponse()
  : _code(0)
  , _headers(LinkedList<AsyncWebHeader *>([](AsyncWebHeader *h){ delete h; }))
  , _contentType()
  , _contentLength(0)
  , _sendContentLength(true)
  , _chunked(false)
  , _headLength(0)
  , _sentLength(0)
  , _ackedLength(0)
  , _writtenLength(0)
  , _state(RESPONSE_SETUP)
{}

AsyncWebServerResponse::~AsyncWebServerResponse(){
  _headers.free();
}

void AsyncWebServerResponse::setCode(int code){
  if(_state == RESPONSE_SETUP)
    _code = code;
}

void AsyncWebServerResponse::setContentLength(size_t len){
  if(_state == RESPONSE_SETUP)
    _contentLength = len;
}

void AsyncWebServerResponse::setContentType(const String& type){
  if(_state == RESPONSE_SETUP)
    _contentType = type;
}

void AsyncWebServerResponse::addHeader(const String& name, const String& value){
  _headers.add(new AsyncWebHeader(name, value));
}

String AsyncWebServerResponse::_assembleHead(uint8_t version){
  if(version){
    addHeader("Accept-Ranges","none");
    if(_chunked)
      addHeader("Transfer-Encoding","chunked");
  }
  String out = String();
  int bufSize = 300;
  char buf[bufSize];

  snprintf(buf, bufSize, "HTTP/1.%d %d %s\r\n", version, _code, _responseCodeToString(_code));
  out.concat(buf);

  if(_sendContentLength) {
    snprintf(buf, bufSize, "Content-Length: %d\r\n", _contentLength);
    out.concat(buf);
  }
  if(_contentType.length()) {
    snprintf(buf, bufSize, "Content-Type: %s\r\n", _contentType.c_str());
    out.concat(buf);
  }

  for(const auto& header: _headers){
    snprintf(buf, bufSize, "%s: %s\r\n", header->name().c_str(), header->value().c_str());
    out.concat(buf);
  }
  _headers.free();

  out.concat("\r\n");
  _headLength = out.length();
  return out;
}

bool AsyncWebServerResponse::_started() const { return _state > RESPONSE_SETUP; }
bool AsyncWebServerResponse::_finished() const { return _state > RESPONSE_WAIT_ACK; }
bool AsyncWebServerResponse::_failed() const { return _state == RESPONSE_FAILED; }
bool AsyncWebServerResponse::_sourceValid() const { return false; }
void AsyncWebServerResponse::_respond(AsyncWebServerRequest *request){ _state = RESPONSE_END; request->client()->close(); }
size_t AsyncWebServerResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time){ return 0; }

/*
 * String/Code Response
 * */
AsyncBasicResponse::AsyncBasicResponse(int code, const String& contentType, const String& content){
  _code = code;
  _content = content;
  _contentType = contentType;
  if(_content.length()){
    _contentLength = _content.length();
    if(!_contentType.length())
      _contentType = "text/plain";
  }
  addHeader("Connection","close");
}

void AsyncBasicResponse::_respond(AsyncWebServerRequest *request){
  _state = RESPONSE_HEADERS;
  String out = _assembleHead(request->version());
  size_t outLen = out.length();
  size_t space = request->client()->space();
  if(!_contentLength && space >= outLen){
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_WAIT_ACK;
  } else if(_contentLength && space >= outLen + _contentLength){
    out += _content;
    outLen += _contentLength;
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_WAIT_ACK;
  } else if(space && space < out.length()){
    String partial = out.substring(0, space);
    _content = out.substring(space) + _content;
    _contentLength += outLen - space;
    _writtenLength += request->client()->write(partial.c_str(), partial.length());
    _state = RESPONSE_CONTENT;
  } else if(space > outLen && space < (outLen + _contentLength)){
    size_t shift = space - outLen;
    outLen += shift;
    _sentLength += shift;
    out += _content.substring(0, shift);
    _content = _content.substring(shift);
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_CONTENT;
  } else {
    _content = out + _content;
    _contentLength += outLen;
    _state = RESPONSE_CONTENT;
  }
}

size_t AsyncBasicResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time){
  _ackedLength += len;
  if(_state == RESPONSE_CONTENT){
    size_t available = _contentLength - _sentLength;
    size_t space = request->client()->space();
    //we can fit in this packet
    if(space > available){
      _writtenLength += request->client()->write(_content.c_str(), available);
      _content = String();
      _state = RESPONSE_WAIT_ACK;
      return available;
    }
    //send some data, the rest on ack
    String out = _content.substring(0, space);
    _content = _content.substring(space);
    _sentLength += space;
    _writtenLength += request->client()->write(out.c_str(), space);
    return space;
  } else if(_state == RESPONSE_WAIT_ACK){
    if(_ackedLength >= _writtenLength){
      _state = RESPONSE_END;
    }
  }
  return 0;
}


/*
 * Abstract Response
 * */

void AsyncAbstractResponse::_respond(AsyncWebServerRequest *request){
  addHeader("Connection","close");
  _head = _assembleHead(request->version());
  _state = RESPONSE_HEADERS;
  _ack(request, 0, 0);
}

size_t AsyncAbstractResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time){
  if(!_sourceValid()){
    _state = RESPONSE_FAILED;
    request->client()->close();
    return 0;
  }
  _ackedLength += len;
  size_t space = request->client()->space();

  size_t headLen = _head.length();
  if(_state == RESPONSE_HEADERS){
    if(space >= headLen){
      _state = RESPONSE_CONTENT;
      space -= headLen;
    } else {
      String out = _head.substring(0, space);
      _head = _head.substring(space);
      _writtenLength += request->client()->write(out.c_str(), out.length());
      return out.length();
    }
  }

  if(_state == RESPONSE_CONTENT){
    size_t outLen;
    if(_chunked){
      if(space <= 8){
        return 0;
      }
      outLen = space;
    } else if(!_sendContentLength){
      outLen = space;
    } else {
      outLen = ((_contentLength - _sentLength) > space)?space:(_contentLength - _sentLength);
    }

    uint8_t *buf = (uint8_t *)malloc(outLen+headLen);
    if (!buf) {
      // os_printf("_ack malloc %d failed\n", outLen+headLen);
      return 0;
    }

    if(headLen){
      sprintf((char*)buf, "%s", _head.c_str());
      _head = String();
    }

    size_t readLen = 0;

    if(_chunked){
      readLen = _fillBuffer(buf+headLen, outLen - 8);
      char pre[6];
      sprintf(pre, "%x\r\n", readLen);
      size_t preLen = strlen(pre);
      memmove(buf+headLen+preLen, buf+headLen, readLen);
      for(size_t i=0; i<preLen; i++)
        buf[i+headLen] = pre[i];
      outLen = preLen + readLen + headLen;
      buf[outLen++] = '\r';
      buf[outLen++] = '\n';
    } else {
      outLen = _fillBuffer(buf+headLen, outLen) + headLen;
    }

    if(outLen)
      _writtenLength += request->client()->write((const char*)buf, outLen);

    if(_chunked)
      _sentLength += readLen;
    else
      _sentLength += outLen - headLen;

    free(buf);

    if((_chunked && readLen == 0) || (!_sendContentLength && outLen == 0) || _sentLength == _contentLength){
      _state = RESPONSE_WAIT_ACK;
    }
    return outLen;

  } else if(_state == RESPONSE_WAIT_ACK){
    if(!_sendContentLength || _ackedLength >= _writtenLength){
      _state = RESPONSE_END;
      if(!_chunked && !_sendContentLength)
        request->client()->close(true);
    }
  }
  return 0;
}




/*
 * File Response
 * */

AsyncFileResponse::~AsyncFileResponse(){
  if(_content)
    _content.close();
}

void AsyncFileResponse::_setContentType(const String& path){
  if (path.endsWith(".html")) _contentType = String(F("text/html"));
  else if (path.endsWith(".htm")) _contentType = String(F("text/html"));
  else if (path.endsWith(".css")) _contentType = String(F("text/css"));
  else if (path.endsWith(".json")) _contentType = String(F("text/json"));
  else if (path.endsWith(".js")) _contentType = String(F("application/javascript"));
  else if (path.endsWith(".png")) _contentType = String(F("image/png"));
  else if (path.endsWith(".gif")) _contentType = String(F("image/gif"));
  else if (path.endsWith(".jpg")) _contentType = String(F("image/jpeg"));
  else if (path.endsWith(".ico")) _contentType = String(F("image/x-icon"));
  else if (path.endsWith(".svg")) _contentType = String(F("image/svg+xml"));
  else if (path.endsWith(".eot")) _contentType = String(F("font/eot"));
  else if (path.endsWith(".woff")) _contentType = String(F("font/woff"));
  else if (path.endsWith(".woff2")) _contentType = String(F("font/woff2"));
  else if (path.endsWith(".ttf")) _contentType = String(F("font/ttf"));
  else if (path.endsWith(".xml")) _contentType = String(F("text/xml"));
  else if (path.endsWith(".pdf")) _contentType = String(F("application/pdf"));
  else if (path.endsWith(".zip")) _contentType = String(F("application/zip"));
  else if(path.endsWith(".gz")) _contentType = String(F("application/x-gzip"));
  else _contentType = String(F("text/plain"));
}

AsyncFileResponse::AsyncFileResponse(FS &fs, const String& path, const String& contentType, bool download){
  _code = 200;
  _path = path;

  if(!download && !fs.exists(_path) && fs.exists(_path+".gz")){
    _path = _path+".gz";
    addHeader(String(F("Content-Encoding")), "gzip");
  }

  _content = fs.open(_path, "r");
  _contentLength = _content.size();

  if(contentType == "")
    _setContentType(path);
  else
    _contentType = contentType;

  int filenameStart = path.lastIndexOf('/') + 1;
  char buf[26+path.length()-filenameStart];
  char* filename = (char*)path.c_str() + filenameStart;

  if(download) {
    // set filename and force download
    snprintf(buf, sizeof (buf), "attachment; filename=\"%s\"", filename);
  } else {
    // set filename and force rendering
    snprintf(buf, sizeof (buf), "inline; filename=\"%s\"", filename);
  }
  addHeader("Content-Disposition", buf);

}

AsyncFileResponse::AsyncFileResponse(File content, const String& path, const String& contentType, bool download){
  _code = 200;
  _path = path;
  _content = content;
  _contentLength = _content.size();

  if(!download && String(_content.name()).endsWith(".gz") && !path.endsWith(".gz"))
    addHeader(String(F("Content-Encoding")), "gzip");

  if(contentType == "")
    _setContentType(path);
  else
    _contentType = contentType;

  int filenameStart = path.lastIndexOf('/') + 1;
  char buf[26+path.length()-filenameStart];
  char* filename = (char*)path.c_str() + filenameStart;

  if(download) {
    snprintf(buf, sizeof (buf), "attachment; filename=\"%s\"", filename);
  } else {
    snprintf(buf, sizeof (buf), "inline; filename=\"%s\"", filename);
  }
  addHeader("Content-Disposition", buf);
}

size_t AsyncFileResponse::_fillBuffer(uint8_t *data, size_t len){
  _content.read(data, len);
  return len;
}

/*
 * Stream Response
 * */

AsyncStreamResponse::AsyncStreamResponse(Stream &stream, const String& contentType, size_t len){
  _code = 200;
  _content = &stream;
  _contentLength = len;
  _contentType = contentType;
}

size_t AsyncStreamResponse::_fillBuffer(uint8_t *data, size_t len){
  size_t available = _content->available();
  size_t outLen = (available > len)?len:available;
  size_t i;
  for(i=0;i<outLen;i++)
    data[i] = _content->read();
  return outLen;
}

/*
 * Callback Response
 * */

AsyncCallbackResponse::AsyncCallbackResponse(const String& contentType, size_t len, AwsResponseFiller callback){
  _code = 200;
  _content = callback;
  _contentLength = len;
  if(!len)
    _sendContentLength = false;
  _contentType = contentType;
}

size_t AsyncCallbackResponse::_fillBuffer(uint8_t *data, size_t len){
  return _content(data, len, _sentLength);
}

/*
 * Chunked Response
 * */

AsyncChunkedResponse::AsyncChunkedResponse(const String& contentType, AwsResponseFiller callback){
  _code = 200;
  _content = callback;
  _contentLength = 0;
  _contentType = contentType;
  _sendContentLength = false;
  _chunked = true;
}

size_t AsyncChunkedResponse::_fillBuffer(uint8_t *data, size_t len){
  return _content(data, len, _sentLength);
}

/*
 * Progmem Response
 * */

AsyncProgmemResponse::AsyncProgmemResponse(int code, const String& contentType, const uint8_t * content, size_t len){
  _code = code;
  _content = content;
  _contentType = contentType;
  _contentLength = len;
}

size_t AsyncProgmemResponse::_fillBuffer(uint8_t *data, size_t len){
  size_t left = _contentLength - _sentLength;
    if (left > len) {
      memcpy_P(data, _content + _sentLength, len);
      return len;
    }
    memcpy_P(data, _content + _sentLength, left);
    return left;
}


/*
 * Response Stream (You can print/write/printf to it, up to the contentLen bytes)
 * */

AsyncResponseStream::AsyncResponseStream(const String& contentType, size_t bufferSize){
  _code = 200;
  _contentLength = 0;
  _contentType = contentType;
  _content = new cbuf(bufferSize);
}

AsyncResponseStream::~AsyncResponseStream(){
  delete _content;
}

size_t AsyncResponseStream::_fillBuffer(uint8_t *buf, size_t maxLen){
  return _content->read((char*)buf, maxLen);
}

size_t AsyncResponseStream::write(const uint8_t *data, size_t len){
  if(_started())
    return 0;

  if(len > _content->room()){
    size_t needed = len - _content->room();
    _content->resizeAdd(needed);
  }
  size_t written = _content->write((const char*)data, len);
  _contentLength += written;
  return written;
}

size_t AsyncResponseStream::write(uint8_t data){
  return write(&data, 1);
}

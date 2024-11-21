/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <htl_stdinc.hpp>

#include <inttypes.h>
#include <assert.h>

#include <string>
#include <sstream>
using namespace std;

#include <htl_core_error.hpp>
#include <htl_core_log.hpp>

#include <htl_app_http_client.hpp>

StHttpClient::StHttpClient(){
    socket = new StSocket();
    connected_url = NULL;
}

StHttpClient::~StHttpClient(){
    delete socket;
    socket = NULL;

    delete connected_url;
    connected_url = NULL;
}

int StHttpClient::DownloadString(HttpUrl* url, std::string* response){
    int ret = ERROR_SUCCESS;
    
    if((ret = CheckUrl(url)) != ERROR_SUCCESS){
        Error("http client check url failed. ret=%d", ret);
        return ret;
    }
    
    if((ret = Connect(url)) != ERROR_SUCCESS){
        Error("http client connect url failed. ret=%d", ret);
        return ret;
    }

    // send GET request to read content
    // GET %s HTTP/1.1\r\nHost: %s\r\n\r\n
    stringstream ss;
    ss << "GET " << url->GetPath() << " "
        << "HTTP/1.1\r\n"
        << "Host: " << url->GetHost() << ":" << url->GetPort() << "\r\n"
        << "Connection: Keep-Alive" << "\r\n"
        << "User-Agent: " << ProductHTTPName << "\r\n"
        << "\r\n";
    
    ssize_t nwrite;
    if((ret = socket->Write(ss.str().c_str(), ss.str().length(), &nwrite)) != ERROR_SUCCESS){
        Error("write to server failed. ret=%d", ret);
        return ret;
    }
    
    if((ret = ParseResponse(url, response)) != ERROR_SUCCESS){
        Error("http client parse response failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

http_parser* StHttpClient::GetResponseHeader(){
    return &http_header;
}

int StHttpClient::on_headers_complete(http_parser* parser){
    StHttpClient* obj = (StHttpClient*)parser->data;
    obj->OnHeaderCompleted(parser);
    
    // see http_parser.c:1570, return 1 to skip body.
    return 1;
}

void StHttpClient::OnHeaderCompleted(http_parser* parser){
    // save the parser status when header parse completed.
    memcpy(&http_header, parser, sizeof(http_header));
}

int StHttpClient::ParseResponse(HttpUrl* url, string* response){
    int ret = ERROR_SUCCESS;

    int body_received = 0;
    if((ret = ParseResponseHeader(response, body_received)) != ERROR_SUCCESS){
        Error("parse response header failed. ret=%d", ret);
        return ret;
    }

    if((ret = ParseResponseBody(url, response, body_received)) != ERROR_SUCCESS){
        Error("parse response body failed. ret=%d", ret);
        return ret;
    }

    Info("url %s download, body size=%"PRId64, url->GetUrl(), http_header.content_length);
    
    return ret;
}

int StHttpClient::ParseResponseBody(HttpUrl* url, string* response, int body_received){
    int ret = ERROR_SUCCESS;
    
    assert(url != NULL);
    
    uint64_t body_left = http_header.content_length - body_received;
    
    if(response != NULL){
        char buf[HTTP_BODY_BUFFER];
        return ParseResponseBodyData(url, response, (size_t)body_left, (const void*)buf, (size_t)HTTP_BODY_BUFFER);
    }
    else{
        // if ignore response, use shared fast memory.
        static char buf[HTTP_BODY_BUFFER];
        return ParseResponseBodyData(url, response, (size_t)body_left, (const void*)buf, (size_t)HTTP_BODY_BUFFER);
    }
    
    return ret;
}

int StHttpClient::ParseResponseBodyData(HttpUrl* url, string* response, size_t body_left, const void* buf, size_t size){
    int ret = ERROR_SUCCESS;
    
    assert(url != NULL);
    
    while(body_left > 0){
        ssize_t nread;
        if((ret = socket->Read(buf, (size < body_left)? size:body_left, &nread)) != ERROR_SUCCESS){
            Error("read header from server failed. ret=%d", ret);
            return ret;
        }
        
        if(response != NULL && nread > 0){
            response->append((char*)buf, nread);
        }
        
        body_left -= nread;
        Info("read url(%s) content partial %"PRId64"/%"PRId64"", 
            url->GetUrl(), http_header.content_length - body_left, http_header.content_length);
    }
    
    return ret;
}

int StHttpClient::ParseResponseHeader(string* response, int& body_received){
    int ret = ERROR_SUCCESS;

    http_parser_settings settings;
    
    memset(&settings, 0, sizeof(settings));
    settings.on_headers_complete = on_headers_complete;
    
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    // callback object ptr.
    parser.data = (void*)this;
    
    // reset response header.
    memset(&http_header, 0, sizeof(http_header));
    
    // parser header.
    char buf[HTTP_HEADER_BUFFER];
    for(;;){
        ssize_t nread;
        if((ret = socket->Read((const void*)buf, (size_t)sizeof(buf), &nread)) != ERROR_SUCCESS){
            Error("read body from server failed. ret=%d", ret);
            return ret;
        }
        
        ssize_t nparsed = http_parser_execute(&parser, &settings, buf, nread);
        Info("read_size=%d, nparsed=%d", (int)nread, (int)nparsed);

        // check header size.
        if(http_header.nread != 0){
            body_received = nread - nparsed;
            
            Info("http header parsed, size=%d, content-length=%"PRId64", body-received=%d", 
                http_header.nread, http_header.content_length, body_received);
                
            if(response != NULL && body_received > 0){
                response->append(buf + nparsed, body_received);
            }

            return ret;
        }
        
        if(nparsed != nread){
            ret = ERROR_HP_PARSE_RESPONSE;
            Error("parse response error, parsed(%d)!=read(%d), ret=%d", (int)nparsed, (int)nread, ret);
            return ret;
        }
    }
    
    return ret;
}

int StHttpClient::Connect(HttpUrl* url){
    int ret = ERROR_SUCCESS;
    
    if(socket->Status() == SocketConnected){
        return ret;
    }

    string ip;
    if((ret = StUtility::DnsResolve(url->GetHost(), ip)) != ERROR_SUCCESS){
        Error("dns resolve failed. ret=%d", ret);
        return ret;
    }
    
    if((ret = socket->Connect(ip.c_str(), url->GetPort())) != ERROR_SUCCESS){
        Error("connect to server failed. ret=%d", ret);
        return ret;
    }
    
    Info("socket connected on url %s", url->GetUrl());
    
    return ret;
}

int StHttpClient::CheckUrl(HttpUrl* url){
    int ret = ERROR_SUCCESS;
    
    if(connected_url == NULL){
        connected_url = url->Copy();

        if((ret = StUtility::DnsResolve(connected_url->GetHost(), connected_ip)) != ERROR_SUCCESS){
            return ret;
        }
    }
    
    string ip;
    if((ret = StUtility::DnsResolve(url->GetHost(), ip)) != ERROR_SUCCESS){
        return ret;
    }
    
    if(ip != connected_ip || connected_url->GetPort() != url->GetPort()){
        ret = ERROR_HP_EP_CHNAGED;
        Error("invalid url=%s, endpoint change from %s:%d to %s:%d", 
            url->GetUrl(), connected_ip.c_str(), connected_url->GetPort(), ip.c_str(), url->GetPort());
            
        return ret;
    }
    
    return ret;
}


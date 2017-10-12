/*
   Copyright 2017, object_he@yeah.net  All rights reserved.

   Author: object_he@yeah.net

   Last modified: 2017-8-17

   Description:
*/

#include "uv/TcpConnection.h"
#include "uv/TcpServer.h"
#include "uv/Async.h"

using namespace std;
using namespace std::chrono;
using namespace uv;

struct write_arg_t
{
    write_arg_t(shared_ptr<TcpConnection> conn=nullptr,const char* buf = nullptr, unsigned int size = 0, AfterWriteCallback callback = nullptr)
        :connection(conn),
        buf(buf),
        size(size),
        callback(nullptr)
    {

    }
    shared_ptr<TcpConnection> connection;
    const char* buf;
    unsigned int size;
    AfterWriteCallback callback;
};

TcpConnection:: ~TcpConnection()
{
    //libuv 在loop轮询中会检测关闭句柄，delete会导致程序异常退出。
    ::uv_close((uv_handle_t*)client,
        [](uv_handle_t* handle)
    {
        delete handle;
    });
}

TcpConnection::TcpConnection(EventLoop* loop,std::string& name,uv_tcp_t* client,bool isConnected)
    :name(name),
    connected(isConnected),
    loop(loop),
    client(client),
    onMessageCallback(nullptr),
    onConnectCloseCallback(nullptr)
{
    client->data = static_cast<void*>(this);
    ::uv_read_start((uv_stream_t*) client,
    [](uv_handle_t *handle, size_t suggested_size,uv_buf_t *buf)
    {
        buf->base = new char [suggested_size];
#if _MSC_VER
        buf->len = (ULONG)suggested_size;
#else
        buf->len = suggested_size;
#endif
    },
    &TcpConnection::onMesageReceive);
}




void TcpConnection::onMessage(const char* buf,ssize_t size)
{
    if(onMessageCallback)
        onMessageCallback(shared_from_this(),buf,size);
}

void TcpConnection::onClose()
{
    if(onConnectCloseCallback)
        onConnectCloseCallback(name);
}

int TcpConnection::write(const char* buf,unsigned int size,AfterWriteCallback callback)
{
    int rst;
    if(connected)
    {
        write_req_t* req = new write_req_t;
        req->buf = uv_buf_init((char*)buf, size);
        rst = ::uv_write((uv_write_t*) req, (uv_stream_t*) client, &req->buf, 1,
        [](uv_write_t *req, int status)
        {
            if (status)
            {
                cout<< "Write error "<<uv_strerror(status)<<endl;
            }
            write_req_t *wr = (write_req_t*) req;

            //delete [] (wr->buf.base);
            delete wr ;
        });
    }
    else
    {
        rst = -1;
    }
    if(nullptr != callback)
    {
        callback((char*)buf,size);
    }
    return rst;
}

void TcpConnection::writeInLoop(const char* buf,ssize_t size,AfterWriteCallback callback)
{
    Async<struct write_arg_t>* async = new Async<struct write_arg_t>(loop, 
    std::bind([this](Async<struct write_arg_t>* handle, struct write_arg_t * data)
    {
        auto connection = data->connection;
        connection->write(data->buf, data->size, data->callback);
        delete data;
        handle->close();
        delete handle;
    }, 
    std::placeholders::_1, std::placeholders::_2));

    struct write_arg_t* writeArg = new struct write_arg_t(shared_from_this(), buf,size,callback);

    async->setData(writeArg);
    async->runInLoop();
}


void TcpConnection::setElement(shared_ptr<ConnectionElement> conn)
{
    element = conn;
}

std::weak_ptr<ConnectionElement> TcpConnection::Element()
{
    return element;
}

void  TcpConnection::onMesageReceive(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
    auto connection = static_cast<TcpConnection*>(client->data);
    if (nread > 0)
    {
        connection->onMessage(buf->base,nread);
        delete [] (buf->base);
        return;
    }
    else if (nread < 0)
    {
        connection->setConnectState(false);
        cout<< uv_err_name((int)nread)<<endl;
        delete [] (buf->base);

        if (nread != UV_EOF)
        {
            connection->onClose();
            return;
        }

        uv_shutdown_t* sreq = new uv_shutdown_t;
        sreq->data = static_cast<void*>(connection);
        ::uv_shutdown(sreq,(uv_stream_t*)client,
        [](uv_shutdown_t* req, int status)
        {
            auto connection = static_cast<TcpConnection*>(req->data);
            connection->onClose();
            delete req;
        });
    }
    else
    {
        /* Everything OK, but nothing read. */
        delete [] (buf->base);
    }

}

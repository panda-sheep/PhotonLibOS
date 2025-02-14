/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <gtest/gtest.h>
#include <photon/photon.h>
#include <photon/rpc/rpc.h>
#include <photon/common/utility.h>
#include <photon/common/alog.h>
#include <photon/net/socket.h>

struct TestOperation {
    const static uint32_t IID = 0x1;
    const static uint32_t FID = 0x2;

    struct Request : public photon::rpc::CheckedMessage<> {
        int32_t code;
        photon::rpc::buffer buf;

        PROCESS_FIELDS(code, buf);
    };

    struct Response : public photon::rpc::CheckedMessage<> {
        int32_t code;
        photon::rpc::buffer buf;

        PROCESS_FIELDS(code, buf);
    };
};

class TestRPCServer {
public:
    TestRPCServer() : skeleton(photon::rpc::new_skeleton()),
                      server(photon::net::new_tcp_socket_server()) {
        skeleton->register_service<TestOperation>(this);
    }

    int do_rpc_service(TestOperation::Request* req, TestOperation::Response* resp, IOVector* iov, IStream* stream) {
        resp->code = req->code + 1;
        iov->push_back(req->buf.size());
        iov->memcpy_from(req->buf.addr(), req->buf.size());
        assert(iov->iovcnt() == 1);
        assert(iov->iovec()[0].iov_len == req->buf.size());
        resp->buf.assign(iov->iovec()[0].iov_base, iov->iovec()[0].iov_len);
        return 0;
    }

    int serve(photon::net::ISocketStream* stream) {
        return skeleton->serve(stream, false);
    }

    int run() {
        if (server->bind() < 0) LOG_ERRNO_RETURN(0, -1, "Failed to bind");
        if (server->listen() < 0) LOG_ERRNO_RETURN(0, -1, "Failed to listen");
        server->set_handler({this, &TestRPCServer::serve});
        return server->start_loop(false);
    }

    std::unique_ptr<photon::rpc::Skeleton> skeleton;
    std::unique_ptr<photon::net::ISocketServer> server;
};

TEST(rpc, message) {
    TestRPCServer server;
    ASSERT_EQ(0, server.run());

    auto pool = photon::rpc::new_stub_pool(-1, -1, -1);
    DEFER(delete pool);

    photon::net::EndPoint ep;
    ASSERT_EQ(0, server.server->getsockname(ep));
    ep.addr = photon::net::IPAddr("127.0.0.1");

    auto stub = pool->get_stub(ep, false);
    ASSERT_NE(nullptr, stub);
    DEFER(pool->put_stub(ep, true));

    char send_buf[4096], recv_buf[4096];
    memset(send_buf, 'x', sizeof(send_buf));

    TestOperation::Request req;
    req.code = rand();
    req.buf.assign(send_buf, sizeof(send_buf));
    TestOperation::Response resp;
    resp.buf.assign(recv_buf, sizeof(recv_buf));

    ASSERT_LT(0, stub->call<TestOperation>(req, resp));
    ASSERT_EQ(req.code + 1, resp.code);
    ASSERT_EQ(req.buf.size(), resp.buf.size());
    ASSERT_EQ(0, memcmp(send_buf, recv_buf, sizeof(send_buf)));

    ASSERT_EQ(0, server.skeleton->shutdown());
}

int main(int argc, char** arg) {
    if (photon::init(photon::INIT_EVENT_EPOLL, photon::INIT_IO_NONE))
        return -1;
    DEFER(photon::fini());
    ::testing::InitGoogleTest(&argc, arg);
    return RUN_ALL_TESTS();
}

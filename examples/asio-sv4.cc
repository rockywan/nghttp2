#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <future>
#include <deque>

#include <nghttp2/asio_http2_server.h>

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

struct Stream : public std::enable_shared_from_this<Stream> {
  Stream(const request &req, const response &res,
         boost::asio::io_service &io_service)
      : io_service(io_service), req(req), res(res), closed(false) {}
  void commit_result() {
    auto self = shared_from_this();
    io_service.post([self]() {
      std::lock_guard<std::mutex> lg(self->mu);
      if (self->closed) {
        return;
      }

      self->res.write_head(200);
      self->res.end("done");
    });
  }
  void set_closed(bool f) {
    std::lock_guard<std::mutex> lg(mu);
    closed = f;
  }

  boost::asio::io_service &io_service;
  std::mutex mu;
  const request &req;
  const response &res;
  bool closed;
};

struct Queue {
  void push(std::shared_ptr<Stream> st) {
    std::lock_guard<std::mutex> lg(mu);
    q.push_back(st);
    cv.notify_all();
  }
  std::shared_ptr<Stream> pop() {
    std::unique_lock<std::mutex> ulk(mu);
    cv.wait(ulk, [this]() { return !q.empty(); });

    auto res = q.front();
    q.pop_front();
    return res;
  }

  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::shared_ptr<Stream>> q;
};

int main(int argc, char **argv) {
  http2 server;

  server.num_threads(2);

  Queue q;

  for (int i = 0; i < 10; ++i) {
    auto th = std::thread([&q]() {
      for (;;) {
        auto st = q.pop();
        sleep(1);
        st->commit_result();
      }
    });
    th.detach();
  }

  server.handle("/", [&q](const request &req, const response &res) {
    auto &io_service = res.io_service();
    auto st = std::make_shared<Stream>(req, res, io_service);
    // req.on_data([]());
    res.on_close([&st](uint32_t error_code) { st->set_closed(true); });
    q.push(st);
  });

  boost::system::error_code ec;
  if (server.listen_and_serve(ec, "0.0.0.0", "8088")) {
    std::cerr << "error: " << ec.message() << std::endl;
  }
}
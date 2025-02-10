#include <iostream>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <jsoncpp/json/json.h>
using namespace boost::asio;
using namespace boost::beast;
namespace http = boost::beast::http;

#define DISCOVERY_SERVER "localhost"
#define DISCOVERY_PORT "8080"

// Function to send an HTTP POST request
bool registerPeer(io_context &ioc, const std::string &id, const std::string &ip, const std::string &port)
{
    try
    {
        ip::tcp::resolver resolver(ioc);
        ip::tcp::socket socket(ioc);

        auto endpoints = resolver.resolve(DISCOVERY_SERVER, DISCOVERY_PORT);
        connect(socket, endpoints);

        // Correctly format JSON using JSONCPP
        Json::Value body;
        body["id"] = id;
        body["ip"] = ip;
        body["port"] = port;
        Json::FastWriter writer;
        std::string body_str = writer.write(body);

        http::request<http::string_body> req(http::verb::post, "/register", 11);
        req.set(http::field::host, DISCOVERY_SERVER);
        req.set(http::field::content_type, "application/json");
        req.set(http::field::content_length, std::to_string(body_str.size()));
        req.body() = body_str;
        req.prepare_payload();

        http::write(socket, req);

        flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(socket, buffer, res);

        return res.result() == http::status::ok;
    }
    catch (std::exception &e)
    {
        std::cerr << "[Client] Registration failed: " << e.what() << std::endl;
        return false;
    }
}

// Function to send an HTTP GET request
std::string fetchPeers(io_context &ioc)
{
    try
    {
        ip::tcp::resolver resolver(ioc);
        ip::tcp::socket socket(ioc);

        auto endpoints = resolver.resolve(DISCOVERY_SERVER, DISCOVERY_PORT);
        connect(socket, endpoints);

        http::request<http::empty_body> req(http::verb::get, "/peers", 11);
        req.set(http::field::host, DISCOVERY_SERVER);

        http::write(socket, req);

        flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(socket, buffer, res);

        return boost::beast::buffers_to_string(res.body().data());
    }
    catch (std::exception &e)
    {
        std::cerr << "[Client] Fetching peers failed: " << e.what() << std::endl;
        return "";
    }
}

int main()
{
    io_context ioc;

    std::string peerId = "peer1";
    std::string peerIp = "192.168.1.100";
    std::string peerPort = "5000";

    if (registerPeer(ioc, peerId, peerIp, peerPort))
    {
        std::cout << "[Client] Successfully registered with discovery server!" << std::endl;
    }
    else
    {
        std::cerr << "[Client] Registration failed!" << std::endl;
        return 1;
    }

    std::cout << "[Client] Fetching peer list..." << std::endl;
    std::string peerList = fetchPeers(ioc);
    std::cout << "[Client] Peers: " << peerList << std::endl;

    return 0;
}

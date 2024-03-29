#pragma once
#include <boost/asio/io_context.hpp>
#include <sdbusplus/asio/connection.hpp>

namespace crow
{
namespace connections
{

// Initialze before using!
// Please see webserver_main for the example how this variable is initialzed,
extern sdbusplus::asio::connection* systemBus;

} // namespace connections
} // namespace crow

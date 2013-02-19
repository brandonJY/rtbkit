/* router_runner.cc
   Jeremy Barnes, 13 December 2012
   Copyright (c) 2012 Datacratic.  All rights reserved.

   Tool to run the router.
*/

#include "router_runner.h"

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp>

#include "rtbkit/core/router/router.h"
#include "rtbkit/core/banker/slave_banker.h"
#include "jml/arch/timers.h"
#include "jml/utils/file_functions.h"

using namespace std;
using namespace ML;
using namespace Datacratic;
using namespace RTBKIT;

static inline Json::Value loadJsonFromFile(const std::string & filename)
{
    ML::File_Read_Buffer buf(filename);
    return Json::parse(std::string(buf.start(), buf.end()));
}


/*****************************************************************************/
/* ROUTER RUNNER                                                             */
/*****************************************************************************/


RouterRunner::
RouterRunner()
    : lossSeconds(15.0)
{
}

void
RouterRunner::
doOptions(int argc, char ** argv,
          const boost::program_options::options_description & opts)
{
    using namespace boost::program_options;

    options_description router_options("Router options");

    router_options.add_options()
        ("zookeeper-uri,Z", value(&zookeeperUri),
         "URI of zookeeper to use")
        ("installation,I", value(&installation),
         "Name of the installation that is running")
        ("node-name,N", value(&nodeName),
         "Name of the node we're running")
        ("loss-seconds,l", value<float>(&lossSeconds),
         "number of seconds after which a loss is assumed")
        ("log-uri", value<vector<string> >(&logUris),
         "URI to publish logs to")
        ("carbon-connection,c", value<vector<string> >(&carbonUris),
         "URI of connection to carbon daemon")
        ("exchange-configuration,x", value<string>(&exchangeConfigurationFile),
         "configuration file with exchange data");

    options_description all_opt = opts;
    all_opt
        .add(router_options);
    all_opt.add_options()
        ("help,h", "print this message");
    
    variables_map vm;
    store(command_line_parser(argc, argv)
          .options(all_opt)
          //.positional(p)
          .run(),
          vm);
    notify(vm);

    if (vm.count("help")) {
        cerr << all_opt << endl;
        exit(1);
    }

    if (installation.empty()) {
        cerr << "'installation' parameter is required" << endl;
        exit(1);
    }

    if (nodeName.empty()) {
        cerr << "'node-name' parameter is required" << endl;
        exit(1);
    }
}

void
RouterRunner::
init()
{
    string servicePrefix("router");
    proxies = std::make_shared<ServiceProxies>();
    proxies->useZookeeper(zookeeperUri, installation);
    if (!carbonUris.empty())
        proxies->logToCarbon(carbonUris, installation + "." + nodeName);
    
    banker = std::make_shared<SlaveBanker>(proxies->zmqContext,
                                             proxies->config,
                                             servicePrefix + ".slaveBanker");
        
    exchangeConfig = loadJsonFromFile(exchangeConfigurationFile);

    router = std::make_shared<Router>(proxies, servicePrefix);
    router->init();
    router->setBanker(banker);
    router->bindTcp();
}

void
RouterRunner::
start()
{
    banker->start();
    router->start();

    // Start all exchanges
    for (auto & exchange: exchangeConfig)
        ExchangeConnector::startExchange(router, exchange);
}

void
RouterRunner::
shutdown()
{
    router->shutdown();
    banker->shutdown();
}

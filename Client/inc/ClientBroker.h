#pragma once
#include "BackTrader.h"
#include "Broker.h"
#include "Client.h"
#include "Order.h"
#include "Position.h"
#include <map>

struct ExecutionFilter;

class ClientBroker : public ClientSpace::Client, public BTBroker
{
    friend class ClientBrain;

public:
    ClientBroker( const std::shared_ptr<EClientSocket>&,
                  const std::shared_ptr<ClientSpace::State>&,
                  const std::shared_ptr<OrderId>& );
    ~ClientBroker() = default;

    void placeOrder( std::pair<Contract, Order>& );
    void filledOrder( long, const ExecutionFilter& );

private:
    /// When servicing execDetails callbacks, this will map the id back to a
    /// corresponding contract
    std::map<long, std::pair<Contract, Order>> executionMap;
    /// Contains all orders that have been submitted to IB but have not been
    /// accepted
    std::set<std::pair<Contract, Order>, OrderCompare> pendingOrders;
};
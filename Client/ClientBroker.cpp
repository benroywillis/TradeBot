#include "ClientBroker.h"
#include "EClientSocket.h"
#include "Execution.h"
#include <spdlog/spdlog.h>

using namespace std;

ClientBroker::ClientBroker( const std::shared_ptr<EClientSocket>&      newClient,
                            const std::shared_ptr<ClientSpace::State>& newState,
                            const shared_ptr<OrderId>&                 ordersId )
{
    p_Client = newClient;
    p_State = newState;
    p_OrderId = ordersId;
    executionMap = map<long, pair<Contract, Order>>();
    pendingOrders = set<pair<Contract, Order>, OrderCompare>();
    openOrders = set<pair<Contract, Order>, OrderCompare>();
}

void ClientBroker::placeOrder( pair<Contract, Order>& p )
{
    auto contract = p.first;
    auto order = p.second;
    spdlog::info( "Placing order for symbol " + contract.symbol + ", SecType " +
                  contract.secType + " with order ID " + to_string( *p_OrderId ) );
    auto newOrderId = *p_OrderId;
    orderMap[newOrderId] = p;
    ( *p_OrderId )++;
    order.orderId = newOrderId;
    openOrders.insert( p );

    p_Client->placeOrder( newOrderId, contract, order );
}

void ClientBroker::filledOrder( long reqId, const ExecutionFilter& filter )
{
    p_Client->reqExecutions( reqId, filter );
}

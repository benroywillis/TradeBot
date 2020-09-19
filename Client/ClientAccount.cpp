#include "ClientAccount.h"
#include "AccountSummaryTags.h"
#include "ContractSamples.h"
#include "EClientSocket.h"
#include <spdlog/spdlog.h>
#include <thread>

using namespace std;
using namespace ClientSpace;

ClientAccount::ClientAccount()
{
    startEquity = 0;
    accReqId = 9001;
    accountID = "";
    accountCurrency = "";
}

ClientAccount::ClientAccount( double                           startingCash,
                              const shared_ptr<EClientSocket>& newClient,
                              const shared_ptr<State>&         newState )
{
    startEquity = startingCash;
    p_Client = newClient;
    p_State = newState;
    accReqId = 9001;
    accountID = "";
    accountCurrency = "";
    valid = false;
}

void ClientAccount::init()
{
    p_Client->reqAccountSummary( accReqId, "All",
                                 AccountSummaryTags::getAllTags() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    p_Client->reqAccountUpdates( true, accountID );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
}

void ClientAccount::closeSubscriptions()
{
    *p_State = ACCOUNTCLOSE;
    p_Client->cancelAccountSummary( accReqId );
    p_Client->reqAccountUpdates( false, accountID );
    *p_State = ACCOUNTCLOSESUCCESS;
}

void ClientAccount::addClient( std::shared_ptr<EClientSocket> newClient )
{
    p_Client = move( newClient );
}
void ClientAccount::addState( std::shared_ptr<ClientSpace::State> newState )
{
    p_State = move( newState );
}
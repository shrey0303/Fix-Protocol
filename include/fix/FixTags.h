#ifndef FIX_TAGS_H
#define FIX_TAGS_H

#include <string_view>

namespace fix {

constexpr char SOH = '\x01';

namespace tag {

// Header / trailer
constexpr int BeginString       = 8;
constexpr int BodyLength        = 9;
constexpr int CheckSum          = 10;
constexpr int MsgSeqNum         = 34;
constexpr int MsgType           = 35;
constexpr int SenderCompID      = 49;
constexpr int SendingTime       = 52;
constexpr int TargetCompID      = 56;

// Order
constexpr int AvgPx             = 6;
constexpr int ClOrdID           = 11;
constexpr int CumQty            = 14;
constexpr int ExecID            = 17;
constexpr int HandlInst         = 21;
constexpr int LastPx            = 31;
constexpr int LastQty           = 32;
constexpr int NewSeqNo          = 36;
constexpr int OrderID           = 37;
constexpr int OrderQty          = 38;
constexpr int OrdStatus         = 39;
constexpr int OrdType           = 40;
constexpr int OrigClOrdID       = 41;
constexpr int Price             = 44;
constexpr int RefSeqNum         = 45;
constexpr int Side              = 54;
constexpr int Symbol            = 55;
constexpr int Text              = 58;
constexpr int TimeInForce       = 59;
constexpr int TransactTime      = 60;

// Session
constexpr int EncryptMethod     = 98;
constexpr int HeartBtInt        = 108;
constexpr int TestReqID         = 112;
constexpr int GapFillFlag       = 123;
constexpr int ExecType          = 150;
constexpr int LeavesQty         = 151;

// Reject
constexpr int RefTagID          = 371;
constexpr int RefMsgType        = 372;
constexpr int SessionRejectReason = 373;

// Market data — repeating groups
constexpr int NoMDEntries       = 268;
constexpr int MDEntryType       = 269;  // delimiter tag for MD group
constexpr int MDEntryPx         = 270;
constexpr int MDEntrySize       = 271;
constexpr int MDUpdateAction    = 279;

} // namespace tag

namespace msgtype {

constexpr std::string_view Heartbeat              = "0";
constexpr std::string_view TestRequest            = "1";
constexpr std::string_view ResendRequest          = "2";
constexpr std::string_view Reject                 = "3";
constexpr std::string_view SequenceReset          = "4";
constexpr std::string_view Logout                 = "5";
constexpr std::string_view Logon                  = "A";
constexpr std::string_view NewOrderSingle         = "D";
constexpr std::string_view OrderCancelRequest     = "F";
constexpr std::string_view OrderCancelReplace     = "G";
constexpr std::string_view ExecutionReport        = "8";
constexpr std::string_view MarketDataSnapshot     = "W";

} // namespace msgtype

} // namespace fix

#endif

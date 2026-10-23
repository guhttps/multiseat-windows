// Experimental wire policy only. No device is registered/opened and no caller
// is authenticated by this header. Runtime admission remains unavailable.
#pragma once
#include <windows.h>
#include <winioctl.h>
#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>

namespace multiseat::native::channel {
inline constexpr GUID InterfaceId{0xb701c723,0x86f9,0x42e9,{0x96,0x10,0x0d,0xc7,0xf6,0xf3,0xd3,0x86}};
inline constexpr wchar_t Reference[]=L"ms-status";
inline constexpr wchar_t FileName[]=L"\\ms-status";
inline constexpr DWORD Control=CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA);
inline constexpr DWORD Magic=0x4449534d;
inline constexpr WORD Version=2;
enum class Operation:WORD {Hello=1,Status=2};
enum Flags:DWORD {Configured=1,WorkerInitialized=2,ReadbackCompleted=4};
// Fixed-width diagnostics only: no pointer, borrowed handle, path or pixels.
#pragma pack(push,1)
struct Request {
 DWORD magic;WORD version;Operation operation;DWORD bytes;DWORD session;
 GUID admission;GUID device;ULONGLONG sequence;
};
struct Reply {
 DWORD magic;WORD version;Operation operation;DWORD bytes;DWORD session;
 GUID admission;GUID device;ULONGLONG sequence;
 DWORD flags;DWORD width;DWORD height;DWORD reserved;
 ULONGLONG frames;ULONGLONG checksum;HRESULT status;DWORD reserved2;ULONGLONG epoch;
};
#pragma pack(pop)
static_assert(sizeof(Request)==56&&sizeof(Reply)==104);
static_assert(offsetof(Request,sequence)==48&&offsetof(Reply,epoch)==96);
static_assert(std::is_trivially_copyable_v<Request>&&std::is_trivially_copyable_v<Reply>);
inline bool Same(const GUID& a,const GUID& b){return !std::memcmp(&a,&b,sizeof(a));}
inline bool Zero(const GUID& id){return Same(id,GUID{});}
// Reserved-ID policy only; this cannot identify the current physical console.
inline bool Target(DWORD session){return session>1&&session!=MAXDWORD;}
inline bool Decode(std::span<const std::byte> bytes,Request& output){
 output={};if(bytes.size()!=sizeof(Request))return false;Request value{};std::memcpy(&value,bytes.data(),sizeof(value));
 if(value.magic!=Magic||value.version!=Version||value.bytes!=sizeof(value)||!Target(value.session)||Zero(value.admission)||!value.sequence)return false;
 if(value.operation==Operation::Hello){if(!Zero(value.device)||value.sequence!=1)return false;}
 else if(value.operation==Operation::Status){if(Zero(value.device)||value.sequence<2)return false;}
 else return false;
 output=value;return true;
}
inline bool Decode(std::span<const std::byte> bytes,Reply& output){
 output={};if(bytes.size()!=sizeof(Reply))return false;Reply value{};std::memcpy(&value,bytes.data(),sizeof(value));
 if(value.magic!=Magic||value.version!=Version||value.bytes!=sizeof(value)||!Target(value.session)||Zero(value.admission)||Zero(value.device)||
    !value.sequence||value.reserved||value.reserved2||(value.flags&~DWORD(Configured|WorkerInitialized|ReadbackCompleted)))return false;
 if(value.operation==Operation::Hello){if(value.sequence!=1)return false;}
 else if(value.operation==Operation::Status){if(value.sequence<2)return false;}
 else return false;
 if((value.flags&WorkerInitialized)&&(!(value.flags&Configured)||!value.epoch))return false;
 if(value.flags&ReadbackCompleted){
  if((value.flags&(Configured|WorkerInitialized))!=(Configured|WorkerInitialized)||!value.frames||!value.width||!value.height||value.width>1280||value.height>720)return false;
 }else if(value.frames||value.checksum||value.width||value.height)return false;
 output=value;return true;
}
template<class T>inline std::span<const std::byte> Packet(const T& value){return std::as_bytes(std::span(&value,1));}

// This state machine accepts only requests whose OS caller was authenticated
// separately by the driver. Serialize it with file cleanup and each exchange.
class Server final {
 const DWORD session;const GUID admission,device;ULONGLONG sequence=0;bool closed=false;
public:
 Server(DWORD target,GUID incarnation,GUID instance):session(target),admission(incarnation),device(instance){}
 bool Accept(const Request& request){
  Request checked{};
  if(closed||!Target(session)||Zero(admission)||Zero(device)||sequence==MAXULONGLONG||!Decode(Packet(request),checked)||
     request.session!=session||!Same(request.admission,admission)||request.sequence!=sequence+1||
     (sequence?request.operation!=Operation::Status||!Same(request.device,device):request.operation!=Operation::Hello)){
   closed=true;return false;
  }
  ++sequence;return true;
 }
 Reply Header(const Request& request)const{
  Reply value{};value.magic=Magic;value.version=Version;value.operation=request.operation;value.bytes=sizeof(value);
  value.session=session;value.admission=admission;value.device=device;value.sequence=request.sequence;value.status=E_PENDING;return value;
 }
 void Close(){closed=true;}
 bool Closed()const{return closed;}
};
// Client state: no retry after ambiguous completion, no sequence/counter reuse.
class Exchange final {
 const DWORD session;const GUID admission;GUID device{};ULONGLONG sequence=0;bool closed=false,pending=false;
 Reply previous{};
public:
 Exchange(DWORD target,GUID incarnation):session(target),admission(incarnation){}
 bool Begin(Request& output){
  output={};if(closed||pending||!Target(session)||Zero(admission)||sequence==MAXULONGLONG){Close();return false;}
  output={Magic,Version,sequence?Operation::Status:Operation::Hello,sizeof(Request),session,admission,device,++sequence};pending=true;return true;
 }
 bool Complete(std::span<const std::byte> packet,Reply& output){
  output={};Reply value{};
  if(closed||!pending||!Decode(packet,value)||value.session!=session||!Same(value.admission,admission)||value.sequence!=sequence||value.status!=S_OK||
     (sequence==1?value.operation!=Operation::Hello:(value.operation!=Operation::Status||!Same(value.device,device)))||
     (sequence>1&&(value.epoch<previous.epoch||(value.epoch==previous.epoch&&
       (value.frames<previous.frames||(value.frames&&value.frames==previous.frames&&
        (value.checksum!=previous.checksum||value.width!=previous.width||value.height!=previous.height))))))){Close();return false;}
  pending=false;if(sequence==1)device=value.device;previous=value;output=value;return true;
 }
 void Close(){closed=true;pending=false;device={};previous={};}
 bool Closed()const{return closed;}
};
}

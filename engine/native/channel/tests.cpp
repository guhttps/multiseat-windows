#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "status-protocol.h"
#include <array>
#include <cstdio>
#include <algorithm>

namespace mswire = multiseat::native::channel;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
constexpr GUID Admission{0x12345678,1,2,{1,2,3,4,5,6,7,8}},Device{0x87654321,3,4,{8,7,6,5,4,3,2,1}};
mswire::Request Hello(){return {mswire::Magic,mswire::Version,mswire::Operation::Hello,sizeof(mswire::Request),2,Admission,{},1};}
mswire::Reply Reply(const mswire::Request& request){
 mswire::Server server(2,Admission,Device);auto value=server.Header(request);value.status=S_OK;return value;
}
template<class T>bool Cleared(const T& object){T zero{};return !std::memcmp(&object,&zero,sizeof(object));}
void Wire(){
 const auto hello=Hello();auto answer=Reply(hello);mswire::Request request{};mswire::Reply reply{};
 Check(mswire::Decode(mswire::Packet(hello),request),"valid wire hello");
 Check(mswire::Decode(mswire::Packet(answer),reply),"valid diagnostic does not claim readiness");
 for(std::size_t length=0;length<sizeof(hello);++length){request=hello;Check(!mswire::Decode(mswire::Packet(hello).first(length),request)&&Cleared(request),"truncated request clears output");}
 for(std::size_t length=0;length<sizeof(answer);++length){reply=answer;Check(!mswire::Decode(mswire::Packet(answer).first(length),reply)&&Cleared(reply),"truncated reply clears output");}
 std::array<std::byte,sizeof(answer)+1> extra{};std::memcpy(extra.data(),&answer,sizeof(answer));
 Check(!mswire::Decode(std::span<const std::byte>(extra),reply),"trailing reply refused");
 auto bad=hello;bad.version=1;Check(!mswire::Decode(mswire::Packet(bad),request),"old wire version refused");
 bad=hello;bad.device=Device;Check(!mswire::Decode(mswire::Packet(bad),request),"hello cannot select device");
 bad=hello;bad.session=1;Check(!mswire::Decode(mswire::Packet(bad),request),"reserved session 1 refused");
 bad=hello;bad.operation=static_cast<mswire::Operation>(3);Check(!mswire::Decode(mswire::Packet(bad),request),"arbitrary command refused");
 answer.flags=mswire::Configured|mswire::WorkerInitialized|mswire::ReadbackCompleted;answer.epoch=1;answer.frames=5;answer.width=1280;answer.height=720;answer.checksum=123;
 Check(mswire::Decode(mswire::Packet(answer),reply),"complete frame observation accepted");
 for(unsigned fault=0;fault<9;++fault){
  auto broken=answer;
  switch(fault){case 0:broken.epoch=0;break;case 1:broken.frames=0;break;case 2:broken.width=1281;break;case 3:broken.height=721;break;
   case 4:broken.flags&=~mswire::WorkerInitialized;break;case 5:broken.flags&=~mswire::Configured;break;case 6:broken.flags=8;break;case 7:broken.reserved=1;break;case 8:broken.reserved2=1;break;}
  Check(!mswire::Decode(mswire::Packet(broken),reply)&&Cleared(reply),"contradictory or reserved frame observation refused");
 }
 // Model the real buffered I/O aliasing contract: copy before zero/output.
 alignas(mswire::Reply) std::array<std::byte,sizeof(mswire::Reply)> shared{};std::memcpy(shared.data(),&hello,sizeof(hello));
 std::memcpy(&request,shared.data(),sizeof(request));std::fill(shared.begin(),shared.end(),std::byte{});
 Check(mswire::Decode(mswire::Packet(request),bad)&&bad.sequence==1,"request survives aliased output clear");
 for(unsigned fault=0;fault<6;++fault){
  mswire::Server server(2,Admission,Device);Check(server.Accept(hello),"server authenticating first sequence policy");
  auto next=hello;next.operation=mswire::Operation::Status;next.device=Device;next.sequence=2;
  switch(fault){case 0:next.sequence=1;break;case 1:next.sequence=3;break;case 2:next.admission.Data1^=1;break;case 3:next.device.Data1^=1;break;
   case 4:next.session=3;break;case 5:next=hello;break;}
  Check(!server.Accept(next)&&server.Closed()&&!server.Accept(hello),"replay/generation/fork permanently closes server");
 }
 for(unsigned fault=0;fault<7;++fault){
  mswire::Exchange client(2,Admission);Check(client.Begin(request),"client starts hello");auto first=answer;
  Check(client.Complete(mswire::Packet(first),reply),"client accepts authentic-shaped first observation policy");
  Check(client.Begin(request),"client starts next sequence");auto next=first;next.operation=mswire::Operation::Status;next.sequence=2;
  switch(fault){case 0:next.epoch=0;break;case 1:--next.frames;break;case 2:++next.checksum;break;case 3:--next.width;break;
   case 4:++next.sequence;break;case 5:next.admission.Data1^=1;break;case 6:next.status=E_ACCESSDENIED;break;}
  Check(!client.Complete(mswire::Packet(next),reply)&&Cleared(reply)&&client.Closed()&&!client.Begin(request),"ambiguous/backwards reply closes client");
 }
 mswire::Exchange client(2,Admission);Check(client.Begin(request)&&client.Complete(mswire::Packet(answer),reply),"epoch fixture established");
 Check(client.Begin(request),"new epoch request");auto next=answer;next.operation=mswire::Operation::Status;next.sequence=2;next.epoch=2;next.frames=1;next.checksum=999;
 Check(client.Complete(mswire::Packet(next),reply),"frame count resets only on newer stream epoch");
 mswire::Exchange pending(2,Admission);Check(pending.Begin(request)&&!pending.Begin(request)&&pending.Closed(),"overlapping request terminal");
 mswire::Exchange absent(1,Admission);Check(!absent.Begin(request)&&Cleared(request)&&absent.Closed(),"invalid client target terminal");
}
}
int main(){Wire();std::printf("Status protocol: %u checks, %u failures; no driver or service endpoint.\n",checks,failures);return failures?1:0;}

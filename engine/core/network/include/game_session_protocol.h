#pragma once
// Bounded, versioned application protocol. Never trust counts/strings from peers.
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <stdexcept>
namespace engine::network::game {
constexpr uint32_t version=1;
enum class Message:uint32_t { Hello=1,Welcome,Ready,Weapon,Ability,Skill,UseItem,Equip,State,Visual,Leave,Reject,Interact };
struct Writer {
 std::vector<uint8_t> bytes;
 template<class T>void pod(T v){auto p=reinterpret_cast<const uint8_t*>(&v);bytes.insert(bytes.end(),p,p+sizeof v);}
 void str(const std::string& s){pod(uint32_t(s.size()));bytes.insert(bytes.end(),s.begin(),s.end());}
 void blob(const std::vector<uint8_t>& b){pod(uint32_t(b.size()));bytes.insert(bytes.end(),b.begin(),b.end());}
 Writer(Message m,uint32_t epoch=0){pod(uint32_t(m));pod(version);pod(epoch);}
};
struct Reader {
 const std::vector<uint8_t>& bytes;size_t at=0;
 template<class T>T pod(){if(at+sizeof(T)>bytes.size())throw std::runtime_error("short game packet");T v;std::memcpy(&v,bytes.data()+at,sizeof v);at+=sizeof v;return v;}
 std::string str(size_t max=1024){auto n=pod<uint32_t>();if(n>max||at+n>bytes.size())throw std::runtime_error("invalid game string");std::string s(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;if(s.find('\0')!=std::string::npos)throw std::runtime_error("embedded null");return s;}
 std::vector<uint8_t> blob(size_t max=65536){auto n=pod<uint32_t>();if(n>max||at+n>bytes.size())throw std::runtime_error("invalid component");std::vector<uint8_t> b(bytes.begin()+at,bytes.begin()+at+n);at+=n;return b;}
 float number(){float f=pod<float>();if(!std::isfinite(f)||std::abs(f)>10000000)throw std::runtime_error("invalid coordinate");return f;}
 bool done()const{return at==bytes.size();}
 Message header(uint32_t& epoch){auto m=pod<uint32_t>();if(pod<uint32_t>()!=version||m<1||m>uint32_t(Message::Interact))throw std::runtime_error("incompatible game protocol");epoch=pod<uint32_t>();return Message(m);}
};
}

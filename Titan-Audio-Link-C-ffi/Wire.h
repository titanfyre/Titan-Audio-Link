#pragma once
#include "Runtime.h"
#include <type_traits>
#include <limits>
template<class T> struct Wire;
template<class T>Json EncodeWire(const T& value){
 if constexpr(std::is_array_v<T>){Json result=Json::array();for(const auto& item:value)result.push_back(EncodeWire(item));return result;}
 else if constexpr(std::is_integral_v<T>){if constexpr(sizeof(T)==8)return std::to_string(value);else return value;}
 else return Wire<T>::Encode(value);
}
template<class T>void DecodeWire(const Json& value,T& out){
 if constexpr(std::is_array_v<T>){Require(value.is_array() && value.size()<=std::extent_v<T>,"Wire array exceeds capacity");for(std::size_t i=0;i<value.size();++i)DecodeWire(value[i],out[i]);}
 else if constexpr(std::is_integral_v<T>){
  if constexpr(sizeof(T)==8){Require(value.is_string(),"64-bit wire fields must be decimal strings");auto text=value.get<std::string>();Require(!text.empty() && text.size()<=21,"Invalid 64-bit decimal");std::size_t consumed=0;try{if constexpr(std::is_signed_v<T>)out=std::stoll(text,&consumed);else{Require(text[0]!='-',"Negative unsigned value");out=std::stoull(text,&consumed);}}catch(const std::exception&){throw SdkError(87,"64-bit wire field out of range");}Require(consumed==text.size(),"Invalid decimal identifier");}
  else {Require(value.is_number_integer(),"Wire field must be integer");if constexpr(std::is_signed_v<T>){if(value.is_number_unsigned())Require(value.get<std::uint64_t>()<=static_cast<std::uint64_t>(std::numeric_limits<T>::max()),"Unsigned input exceeds signed wire field range");auto n=value.get<std::int64_t>();Require(n>=std::numeric_limits<T>::min() && n<=std::numeric_limits<T>::max(),"Signed wire field out of range");out=static_cast<T>(n);}else{Require(value>=0,"Negative unsigned wire field");auto n=value.get<std::uint64_t>();Require(n<=std::numeric_limits<T>::max(),"Unsigned wire field out of range");out=static_cast<T>(n);}}
 }else Wire<T>::Decode(value,out);
}
template<class T>T FromWire(const Json& value){T result{};DecodeWire(value,result);return result;}
template<class T>Json VectorWire(const std::vector<T>& values){Json result=Json::array();for(const auto& value:values)result.push_back(EncodeWire(value));return result;}
inline void CheckCode(unsigned code){if(code)throw SdkError(code,"Native SDK operation failed (Windows "+std::to_string(code)+")");}
inline void CheckMessage(const std::string& error){if(!error.empty())throw SdkError(31,error);}

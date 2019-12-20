// WARNING: This file is machine generated by fidlgen.

#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <memory>

namespace llcpp {

namespace fuchsia {
namespace hardware {
namespace usb {
namespace virtual_ {
namespace bus {

namespace {

[[maybe_unused]]
constexpr uint64_t kBus_Enable_Ordinal = 0x52804a5800000000lu;
[[maybe_unused]]
constexpr uint64_t kBus_Enable_GenOrdinal = 0x1849ded5bdcca327lu;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusEnableRequestTable;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusEnableResponseTable;
extern "C" const fidl_type_t v1_fuchsia_hardware_usb_virtual_bus_BusEnableResponseTable;
[[maybe_unused]]
constexpr uint64_t kBus_Disable_Ordinal = 0x4962c56f00000000lu;
[[maybe_unused]]
constexpr uint64_t kBus_Disable_GenOrdinal = 0x503b1c3fe6290c8lu;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusDisableRequestTable;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusDisableResponseTable;
extern "C" const fidl_type_t v1_fuchsia_hardware_usb_virtual_bus_BusDisableResponseTable;
[[maybe_unused]]
constexpr uint64_t kBus_Connect_Ordinal = 0x6252c09800000000lu;
[[maybe_unused]]
constexpr uint64_t kBus_Connect_GenOrdinal = 0x1ed4d7024d941668lu;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusConnectRequestTable;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusConnectResponseTable;
extern "C" const fidl_type_t v1_fuchsia_hardware_usb_virtual_bus_BusConnectResponseTable;
[[maybe_unused]]
constexpr uint64_t kBus_Disconnect_Ordinal = 0x663c1cc700000000lu;
[[maybe_unused]]
constexpr uint64_t kBus_Disconnect_GenOrdinal = 0x78d72eefd5b46a3blu;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusDisconnectRequestTable;
extern "C" const fidl_type_t fuchsia_hardware_usb_virtual_bus_BusDisconnectResponseTable;
extern "C" const fidl_type_t v1_fuchsia_hardware_usb_virtual_bus_BusDisconnectResponseTable;

}  // namespace
template <>
Bus::ResultOf::Enable_Impl<Bus::EnableResponse>::Enable_Impl(::zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<EnableRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, EnableRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(EnableRequest));
  ::fidl::DecodedMessage<EnableRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Bus::InPlace::Enable(std::move(_client_end), Super::response_buffer()));
}

Bus::ResultOf::Enable Bus::SyncClient::Enable() {
    return ResultOf::Enable(::zx::unowned_channel(this->channel_));
}

Bus::ResultOf::Enable Bus::Call::Enable(::zx::unowned_channel _client_end) {
  return ResultOf::Enable(std::move(_client_end));
}

template <>
Bus::UnownedResultOf::Enable_Impl<Bus::EnableResponse>::Enable_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(EnableRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, EnableRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(EnableRequest));
  ::fidl::DecodedMessage<EnableRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Bus::InPlace::Enable(std::move(_client_end), std::move(_response_buffer)));
}

Bus::UnownedResultOf::Enable Bus::SyncClient::Enable(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Enable(::zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Bus::UnownedResultOf::Enable Bus::Call::Enable(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Enable(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Bus::EnableResponse> Bus::InPlace::Enable(::zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(EnableRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<EnableRequest> params(std::move(_request_buffer));
  Bus::SetTransactionHeaderFor::EnableRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::EnableResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<EnableRequest, EnableResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::EnableResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}

template <>
Bus::ResultOf::Disable_Impl<Bus::DisableResponse>::Disable_Impl(::zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<DisableRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, DisableRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(DisableRequest));
  ::fidl::DecodedMessage<DisableRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Bus::InPlace::Disable(std::move(_client_end), Super::response_buffer()));
}

Bus::ResultOf::Disable Bus::SyncClient::Disable() {
    return ResultOf::Disable(::zx::unowned_channel(this->channel_));
}

Bus::ResultOf::Disable Bus::Call::Disable(::zx::unowned_channel _client_end) {
  return ResultOf::Disable(std::move(_client_end));
}

template <>
Bus::UnownedResultOf::Disable_Impl<Bus::DisableResponse>::Disable_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(DisableRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, DisableRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(DisableRequest));
  ::fidl::DecodedMessage<DisableRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Bus::InPlace::Disable(std::move(_client_end), std::move(_response_buffer)));
}

Bus::UnownedResultOf::Disable Bus::SyncClient::Disable(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Disable(::zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Bus::UnownedResultOf::Disable Bus::Call::Disable(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Disable(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Bus::DisableResponse> Bus::InPlace::Disable(::zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(DisableRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<DisableRequest> params(std::move(_request_buffer));
  Bus::SetTransactionHeaderFor::DisableRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::DisableResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<DisableRequest, DisableResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::DisableResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}

template <>
Bus::ResultOf::Connect_Impl<Bus::ConnectResponse>::Connect_Impl(::zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ConnectRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, ConnectRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(ConnectRequest));
  ::fidl::DecodedMessage<ConnectRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Bus::InPlace::Connect(std::move(_client_end), Super::response_buffer()));
}

Bus::ResultOf::Connect Bus::SyncClient::Connect() {
    return ResultOf::Connect(::zx::unowned_channel(this->channel_));
}

Bus::ResultOf::Connect Bus::Call::Connect(::zx::unowned_channel _client_end) {
  return ResultOf::Connect(std::move(_client_end));
}

template <>
Bus::UnownedResultOf::Connect_Impl<Bus::ConnectResponse>::Connect_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(ConnectRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, ConnectRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(ConnectRequest));
  ::fidl::DecodedMessage<ConnectRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Bus::InPlace::Connect(std::move(_client_end), std::move(_response_buffer)));
}

Bus::UnownedResultOf::Connect Bus::SyncClient::Connect(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Connect(::zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Bus::UnownedResultOf::Connect Bus::Call::Connect(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Connect(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Bus::ConnectResponse> Bus::InPlace::Connect(::zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(ConnectRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<ConnectRequest> params(std::move(_request_buffer));
  Bus::SetTransactionHeaderFor::ConnectRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::ConnectResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<ConnectRequest, ConnectResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::ConnectResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}

template <>
Bus::ResultOf::Disconnect_Impl<Bus::DisconnectResponse>::Disconnect_Impl(::zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<DisconnectRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, DisconnectRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(DisconnectRequest));
  ::fidl::DecodedMessage<DisconnectRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Bus::InPlace::Disconnect(std::move(_client_end), Super::response_buffer()));
}

Bus::ResultOf::Disconnect Bus::SyncClient::Disconnect() {
    return ResultOf::Disconnect(::zx::unowned_channel(this->channel_));
}

Bus::ResultOf::Disconnect Bus::Call::Disconnect(::zx::unowned_channel _client_end) {
  return ResultOf::Disconnect(std::move(_client_end));
}

template <>
Bus::UnownedResultOf::Disconnect_Impl<Bus::DisconnectResponse>::Disconnect_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(DisconnectRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, DisconnectRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(DisconnectRequest));
  ::fidl::DecodedMessage<DisconnectRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Bus::InPlace::Disconnect(std::move(_client_end), std::move(_response_buffer)));
}

Bus::UnownedResultOf::Disconnect Bus::SyncClient::Disconnect(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Disconnect(::zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Bus::UnownedResultOf::Disconnect Bus::Call::Disconnect(::zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Disconnect(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Bus::DisconnectResponse> Bus::InPlace::Disconnect(::zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(DisconnectRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<DisconnectRequest> params(std::move(_request_buffer));
  Bus::SetTransactionHeaderFor::DisconnectRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::DisconnectResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<DisconnectRequest, DisconnectResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Bus::DisconnectResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}


bool Bus::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  zx_status_t status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    txn->Close(status);
    return true;
  }
  switch (hdr->ordinal) {
    case kBus_Enable_Ordinal:
    case kBus_Enable_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<EnableRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Enable(
          Interface::EnableCompleter::Sync(txn));
      return true;
    }
    case kBus_Disable_Ordinal:
    case kBus_Disable_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<DisableRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Disable(
          Interface::DisableCompleter::Sync(txn));
      return true;
    }
    case kBus_Connect_Ordinal:
    case kBus_Connect_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<ConnectRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Connect(
          Interface::ConnectCompleter::Sync(txn));
      return true;
    }
    case kBus_Disconnect_Ordinal:
    case kBus_Disconnect_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<DisconnectRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Disconnect(
          Interface::DisconnectCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool Bus::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}


void Bus::Interface::EnableCompleterBase::Reply(int32_t status) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<EnableResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<EnableResponse*>(_write_bytes);
  Bus::SetTransactionHeaderFor::EnableResponse(
      ::fidl::DecodedMessage<EnableResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              EnableResponse::PrimarySize,
              EnableResponse::PrimarySize)));
  _response.status = std::move(status);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(EnableResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<EnableResponse>(std::move(_response_bytes)));
}

void Bus::Interface::EnableCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status) {
  if (_buffer.capacity() < EnableResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<EnableResponse*>(_buffer.data());
  Bus::SetTransactionHeaderFor::EnableResponse(
      ::fidl::DecodedMessage<EnableResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              EnableResponse::PrimarySize,
              EnableResponse::PrimarySize)));
  _response.status = std::move(status);
  _buffer.set_actual(sizeof(EnableResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<EnableResponse>(std::move(_buffer)));
}

void Bus::Interface::EnableCompleterBase::Reply(::fidl::DecodedMessage<EnableResponse> params) {
  Bus::SetTransactionHeaderFor::EnableResponse(params);
  CompleterBase::SendReply(std::move(params));
}


void Bus::Interface::DisableCompleterBase::Reply(int32_t status) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<DisableResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<DisableResponse*>(_write_bytes);
  Bus::SetTransactionHeaderFor::DisableResponse(
      ::fidl::DecodedMessage<DisableResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              DisableResponse::PrimarySize,
              DisableResponse::PrimarySize)));
  _response.status = std::move(status);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(DisableResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<DisableResponse>(std::move(_response_bytes)));
}

void Bus::Interface::DisableCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status) {
  if (_buffer.capacity() < DisableResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<DisableResponse*>(_buffer.data());
  Bus::SetTransactionHeaderFor::DisableResponse(
      ::fidl::DecodedMessage<DisableResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              DisableResponse::PrimarySize,
              DisableResponse::PrimarySize)));
  _response.status = std::move(status);
  _buffer.set_actual(sizeof(DisableResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<DisableResponse>(std::move(_buffer)));
}

void Bus::Interface::DisableCompleterBase::Reply(::fidl::DecodedMessage<DisableResponse> params) {
  Bus::SetTransactionHeaderFor::DisableResponse(params);
  CompleterBase::SendReply(std::move(params));
}


void Bus::Interface::ConnectCompleterBase::Reply(int32_t status) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ConnectResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<ConnectResponse*>(_write_bytes);
  Bus::SetTransactionHeaderFor::ConnectResponse(
      ::fidl::DecodedMessage<ConnectResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              ConnectResponse::PrimarySize,
              ConnectResponse::PrimarySize)));
  _response.status = std::move(status);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(ConnectResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<ConnectResponse>(std::move(_response_bytes)));
}

void Bus::Interface::ConnectCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status) {
  if (_buffer.capacity() < ConnectResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<ConnectResponse*>(_buffer.data());
  Bus::SetTransactionHeaderFor::ConnectResponse(
      ::fidl::DecodedMessage<ConnectResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              ConnectResponse::PrimarySize,
              ConnectResponse::PrimarySize)));
  _response.status = std::move(status);
  _buffer.set_actual(sizeof(ConnectResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<ConnectResponse>(std::move(_buffer)));
}

void Bus::Interface::ConnectCompleterBase::Reply(::fidl::DecodedMessage<ConnectResponse> params) {
  Bus::SetTransactionHeaderFor::ConnectResponse(params);
  CompleterBase::SendReply(std::move(params));
}


void Bus::Interface::DisconnectCompleterBase::Reply(int32_t status) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<DisconnectResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<DisconnectResponse*>(_write_bytes);
  Bus::SetTransactionHeaderFor::DisconnectResponse(
      ::fidl::DecodedMessage<DisconnectResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              DisconnectResponse::PrimarySize,
              DisconnectResponse::PrimarySize)));
  _response.status = std::move(status);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(DisconnectResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<DisconnectResponse>(std::move(_response_bytes)));
}

void Bus::Interface::DisconnectCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status) {
  if (_buffer.capacity() < DisconnectResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<DisconnectResponse*>(_buffer.data());
  Bus::SetTransactionHeaderFor::DisconnectResponse(
      ::fidl::DecodedMessage<DisconnectResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              DisconnectResponse::PrimarySize,
              DisconnectResponse::PrimarySize)));
  _response.status = std::move(status);
  _buffer.set_actual(sizeof(DisconnectResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<DisconnectResponse>(std::move(_buffer)));
}

void Bus::Interface::DisconnectCompleterBase::Reply(::fidl::DecodedMessage<DisconnectResponse> params) {
  Bus::SetTransactionHeaderFor::DisconnectResponse(params);
  CompleterBase::SendReply(std::move(params));
}



void Bus::SetTransactionHeaderFor::EnableRequest(const ::fidl::DecodedMessage<Bus::EnableRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Enable_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void Bus::SetTransactionHeaderFor::EnableResponse(const ::fidl::DecodedMessage<Bus::EnableResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Enable_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}

void Bus::SetTransactionHeaderFor::DisableRequest(const ::fidl::DecodedMessage<Bus::DisableRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Disable_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void Bus::SetTransactionHeaderFor::DisableResponse(const ::fidl::DecodedMessage<Bus::DisableResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Disable_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}

void Bus::SetTransactionHeaderFor::ConnectRequest(const ::fidl::DecodedMessage<Bus::ConnectRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Connect_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void Bus::SetTransactionHeaderFor::ConnectResponse(const ::fidl::DecodedMessage<Bus::ConnectResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Connect_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}

void Bus::SetTransactionHeaderFor::DisconnectRequest(const ::fidl::DecodedMessage<Bus::DisconnectRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Disconnect_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void Bus::SetTransactionHeaderFor::DisconnectResponse(const ::fidl::DecodedMessage<Bus::DisconnectResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kBus_Disconnect_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}

}  // namespace bus
}  // namespace virtual_
}  // namespace usb
}  // namespace hardware
}  // namespace fuchsia
}  // namespace llcpp

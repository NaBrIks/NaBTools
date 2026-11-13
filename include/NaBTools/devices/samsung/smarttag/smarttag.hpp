// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/config.hpp"
#include "NaBTools/core/action.hpp"
#include "NaBTools/bt/gatt/client.hpp"
#include "NaBTools/devices/samsung/smarttag/owner.hpp"

namespace NaBTools::runtime
{
// Samsung FD5A service data. Names describe the observed protocol, not a
// cryptographically authenticated physical model or permanent identity.
	class SmartTagPacket
	{
		Bytes bytes_;
		explicit SmartTagPacket(Bytes bytes)
		  : bytes_(bytes)
		{
		}

	public:
		static std::optional<SmartTagPacket> parse(AdvertisementView);
		Bytes raw()const
		{
			return bytes_;
		}

		uint8_t version()const
		{
			return bytes_[0] >> 4;
		}

		bool decoded()const
		{
			return bytes_.size() == 20 && version() == 1;
		}

		uint8_t advertisement_type()const
		{
			return (bytes_[0] >> 3) & 1;
		}

		uint8_t state()const
		{
			return bytes_[0] & 7;
		}

		// These fields are available only when decoded() is true.
		uint32_t aging_counter()const;
		Bytes privacy_id()const
		{
			return decoded() ? bytes_.subspan(4, 8) : Bytes{};
		}

		uint8_t region()const
		{
			return decoded() ? bytes_[12] >> 4 : 0;
		}

		bool encrypted()const
		{
			return decoded() && (bytes_[12] & 8);
		}

		bool uwb()const
		{
			return decoded() && (bytes_[12] & 4);
		}

		uint8_t battery_level()const
		{
			return decoded() ? bytes_[12] & 3 : 0;
		}

		Bytes reserved()const
		{
			return decoded() ? bytes_.subspan(13, 3) : Bytes{};
		}

		Bytes signature()const
		{
			return decoded() ? bytes_.subspan(16, 4) : Bytes{};
		}

	};
	const char *smarttag_state_name(uint8_t);
	class SmartTagRecognizer
	{
	public:
		static bool matches(AdvertisementView v)
		{
			return SmartTagPacket::parse(v).has_value();
		}

		static std::optional<Detection> recognize(AdvertisementView);
	};
#if NABTOOLS_SMARTTAG_STRUCT
	struct SmartTagReading {SmartTagPacket packet; Bytes changed_offsets; bool first{};};
	struct SmartTagAction {void *context{}; Delivery (*invoke)(void *, const MessageInfo &, const SmartTagReading &)
						   {
						   };};
	struct SmartTagGattReading {const char *state; const char *field; const char *event_name; const GattEvent &event;};
	struct SmartTagGattAction {void *context{};
		                       Delivery (*invoke)(void *, const MessageInfo &, const SmartTagGattReading &)
							   {
							   };};
	struct SmartTagButtonReading {uint32_t counter; uint8_t action; const char *name;};
	struct SmartTagButtonAction {void *context{};
		                         Delivery (*invoke)(void *, const MessageInfo &, const SmartTagButtonReading &)
								 {
								 };};
#endif
	struct SmartTagActions {
#if NABTOOLS_SMARTTAG_STRUCT
		SmartTagAction reading;
		SmartTagGattAction gatt;
		SmartTagButtonAction button;
#endif
#if NABTOOLS_SMARTTAG_CBOR
		CborAction cbor;
#endif
	};
	class SmartTagDevice final : public IDevice
	{
		Address address_;
		SmartTagActions actions_;
		std::array<uint8_t, Advertisement::capacity> latest_{};
		size_t latest_size_{};
		uint64_t last_seen_{};
		uint32_t packets_{}, changes_{}, delivered_{}, dropped_{};
		IGattClient *gatt_{};
		GattSessionId session_;
		enum class State {disconnected, connecting, discovering, connected, reading, subscribing, disconnecting};
		State state_{State::disconnected};
		struct Attribute {uint16_t handle{}, cccd{}, properties{};};
		// Model, firmware, cipher, owner alarm, button, nonce, encrypted nonce.
		std::array<Attribute, 7> attributes_{};
		uint64_t gatt_sequence_{}, flow_request_{};
		uint8_t read_index_{};
		bool button_subscribed_{};
		SmartTagOwnerAccess owner_access_;
		SmartTagOwnerSession owner_session_;
		enum class OwnerStep {none, sub_nonce, sub_proof, nonce_write, nonce_wait, proof_write, proof_wait, button_sub,
			                  press_write, hold_write, alarm_write};
		OwnerStep owner_step_{OwnerStep::none};
		bool owner_attempted_{}, nonce_seen_{}, proof_seen_{}, button_enabled_{};
		std::array<uint8_t, 16> owner_proof_{};
		mutable std::array<OperationInfo, 9> commands_{};
		mutable std::array<OperationInfo, 4> events_{};
		const char * connection_state()const;
		void emit_gatt(BtEngine &, DeviceId, const GattEvent &, const char *event_name = nullptr);
		Status read_next();
		Status invoke_owner(BtEngine &, DeviceId, const DeviceCommand &);
		bool on_owner_gatt(BtEngine &, DeviceId, const GattEvent &);
		void owner_fault(BtEngine &, DeviceId, const GattEvent &);
		void reset_owner();
		Status write_owner_command(size_t attribute, uint8_t opcode, Bytes arguments);
		void emit_button(BtEngine &, DeviceId, const GattEvent &, uint32_t, uint8_t);
	public:
		SmartTagDevice(Address address, SmartTagActions actions, SmartTagOwnerAccess owner = {})
		  : address_(address), actions_(actions), owner_access_(owner)
		{
		}

		DeviceInfo info()const override;
		InteractionInfo interaction()const override;
		void on_attached(BtEngine &, DeviceId)override;
		void on_gatt(BtEngine &, DeviceId, const GattEvent &)override;
		Status invoke(BtEngine &, DeviceId, const DeviceCommand &)override;
		void on_advertisement(BtEngine &, DeviceId, AdvertisementView)override;
	};
	class SmartTagModule final : public IModule
	{
		SmartTagActions actions_;
		SmartTagOwnerAccess owner_;
	public:
		explicit SmartTagModule(SmartTagActions actions = {}, SmartTagOwnerAccess owner = {})
		  : actions_(actions), owner_(owner)
		{
		}

		const char * key()const override
		{
			return "nabtools.samsung.smarttag";
		}

		void observe(BtEngine &, AdvertisementView, AdStatus)override
		{
		}                                                        // no auto-admission

		std::optional<Detection> recognize(AdvertisementView v)const override
		{
			return SmartTagRecognizer::recognize(v);
		}

		std::unique_ptr<IDevice> create(AdvertisementView)const override;
		DeviceId add_known(BtEngine &, Address)const;
	};
}

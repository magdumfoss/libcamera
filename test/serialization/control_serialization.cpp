/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Serialize and deserialize controls
 */

#include <iostream>

#include <libcamera/camera.h>
#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include <libcamera/ipa/ipa_controls.h>

#include "libcamera/internal/byte_stream_buffer.h"
#include "libcamera/internal/control_serializer.h"

#include "serialization_test.h"
#include "test.h"

using namespace std;
using namespace libcamera;

class ControlSerializationTest : public SerializationTest
{
protected:
	int init() override
	{
		return status_;
	}

	int run() override
	{
		ControlSerializer serializer(ControlSerializer::Role::Proxy);
		ControlSerializer deserializer(ControlSerializer::Role::Worker);

		std::vector<uint8_t> infoData;
		std::vector<uint8_t> listData;

		size_t size;
		int ret;

		/* Create a control list with three controls. */
		const ControlInfoMap &infoMap = camera_->controls();
		ControlList list(infoMap);

		list.set(controls::Brightness, 0.5f);
		list.set(controls::Contrast, 1.2f);
		list.set(controls::Saturation, 0.2f);

		/*
		 * Serialize the control list, this should fail as the control
		 * info map hasn't been serialized.
		 */
		size = serializer.binarySize(list);
		listData.resize(size);
		ByteStreamBuffer buffer(listData.data(), listData.size());

		ret = serializer.serialize(list, buffer);
		if (!ret) {
			cerr << "List serialization without info map should have failed"
			     << endl;
			return TestFail;
		}

		if (buffer.overflow() || buffer.offset()) {
			cerr << "Failed list serialization modified the buffer"
			     << endl;
			return TestFail;
		}

		/* Serialize the control info map. */
		size = serializer.binarySize(infoMap);
		infoData.resize(size);
		buffer = ByteStreamBuffer(infoData.data(), infoData.size());

		ret = serializer.serialize(infoMap, buffer);
		if (ret < 0) {
			cerr << "Failed to serialize ControlInfoMap" << endl;
			return TestFail;
		}

		if (buffer.overflow()) {
			cerr << "Overflow when serializing ControlInfoMap" << endl;
			return TestFail;
		}

		/* Serialize the control list, this should now succeed. */
		size = serializer.binarySize(list);
		listData.resize(size);
		buffer = ByteStreamBuffer(listData.data(), listData.size());

		ret = serializer.serialize(list, buffer);
		if (ret) {
			cerr << "Failed to serialize ControlList" << endl;
			return TestFail;
		}

		if (buffer.overflow()) {
			cerr << "Overflow when serializing ControlList" << endl;
			return TestFail;
		}

		/*
		 * Deserialize the control list, this should fail as the control
		 * info map hasn't been deserialized.
		 */
		buffer = ByteStreamBuffer(const_cast<const uint8_t *>(listData.data()),
					  listData.size());

		ControlList newList = deserializer.deserialize<ControlList>(buffer);
		if (!newList.empty()) {
			cerr << "List deserialization without info map should have failed"
			     << endl;
			return TestFail;
		}

		if (buffer.overflow()) {
			cerr << "Failed list deserialization modified the buffer"
			     << endl;
			return TestFail;
		}

		/* Deserialize the control info map and verify the contents. */
		buffer = ByteStreamBuffer(const_cast<const uint8_t *>(infoData.data()),
					  infoData.size());

		ControlInfoMap newInfoMap = deserializer.deserialize<ControlInfoMap>(buffer);
		if (newInfoMap.empty()) {
			cerr << "Failed to deserialize ControlInfoMap" << endl;
			return TestFail;
		}

		if (buffer.overflow()) {
			cerr << "Overflow when deserializing ControlInfoMap" << endl;
			return TestFail;
		}

		if (!equals(infoMap, newInfoMap)) {
			cerr << "Deserialized map doesn't match original" << endl;
			return TestFail;
		}

		/* Make sure control limits looked up by id are not changed. */
		const ControlInfo &newLimits = newInfoMap.at(&controls::Brightness);
		const ControlInfo &initialLimits = infoMap.at(&controls::Brightness);
		if (newLimits.min() != initialLimits.min() ||
		    newLimits.max() != initialLimits.max()) {
			cerr << "The brightness control limits have changed" << endl;
			return TestFail;
		}

		/* Deserialize the control list and verify the contents. */
		buffer = ByteStreamBuffer(const_cast<const uint8_t *>(listData.data()),
					  listData.size());

		newList = deserializer.deserialize<ControlList>(buffer);
		if (newList.empty()) {
			cerr << "Failed to deserialize ControlList" << endl;
			return TestFail;
		}

		if (buffer.overflow()) {
			cerr << "Overflow when deserializing ControlList" << endl;
			return TestFail;
		}

		if (!equals(list, newList)) {
			cerr << "Deserialized list doesn't match original" << endl;
			return TestFail;
		}

		/* Build a local (V4L2-like) ControlInfoMap and verify name round-trip. */
		vector<unique_ptr<ControlId>> v4l2ControlIds;
		ControlIdMap v4l2IdMap;
		constexpr uint32_t kV4L2TestControlId = 0x009a2001;
		const string kV4L2ControlName = "V4L2_CID_TEST_GAIN";

		v4l2ControlIds.emplace_back(std::make_unique<ControlId>(
			kV4L2TestControlId, kV4L2ControlName, "v4l2",
			ControlTypeInteger32, ControlId::Direction::In));
		v4l2IdMap.emplace(kV4L2TestControlId, v4l2ControlIds.back().get());

		ControlInfoMap::Map v4l2Info;
		v4l2Info.emplace(v4l2ControlIds.back().get(),
				 ControlInfo(ControlValue(int32_t{ 0 }),
					     ControlValue(int32_t{ 255 }),
					     ControlValue(int32_t{ 16 })));
		ControlInfoMap v4l2InfoMap(std::move(v4l2Info), v4l2IdMap);

		ControlSerializer v4l2Serializer(ControlSerializer::Role::Proxy);
		ControlSerializer v4l2Deserializer(ControlSerializer::Role::Worker);

		size = v4l2Serializer.binarySize(v4l2InfoMap);
		infoData.resize(size);
		buffer = ByteStreamBuffer(infoData.data(), infoData.size());

		ret = v4l2Serializer.serialize(v4l2InfoMap, buffer);
		if (ret < 0 || buffer.overflow()) {
			cerr << "Failed to serialize V4L2-like ControlInfoMap" << endl;
			return TestFail;
		}

		buffer = ByteStreamBuffer(const_cast<const uint8_t *>(infoData.data()),
					  infoData.size());
		ControlInfoMap v4l2InfoMapDes =
			v4l2Deserializer.deserialize<ControlInfoMap>(buffer);
		if (v4l2InfoMapDes.empty()) {
			cerr << "Failed to deserialize V4L2-like ControlInfoMap" << endl;
			return TestFail;
		}

		auto idIt = v4l2InfoMapDes.idmap().find(kV4L2TestControlId);
		if (idIt == v4l2InfoMapDes.idmap().end()) {
			cerr << "Deserialized V4L2-like id map misses test control" << endl;
			return TestFail;
		}

		if (idIt->second->name() != kV4L2ControlName) {
			cerr << "Deserialized V4L2-like control name doesn't match" << endl;
			return TestFail;
		}

		/* Reject malformed packets with over-sized names. */
		vector<uint8_t> badNameLenData = infoData;
		auto *badNameLenHeader =
			reinterpret_cast<ipa_controls_header *>(badNameLenData.data());
		auto *badNameLenEntry = reinterpret_cast<ipa_control_info_entry *>(
			badNameLenData.data() + sizeof(*badNameLenHeader));
		badNameLenEntry->name_len = 2048;

		ControlSerializer badNameLenDeserializer(ControlSerializer::Role::Worker);
		buffer = ByteStreamBuffer(const_cast<const uint8_t *>(badNameLenData.data()),
					  badNameLenData.size());
		if (!badNameLenDeserializer.deserialize<ControlInfoMap>(buffer).empty()) {
			cerr << "Oversized control name should be rejected" << endl;
			return TestFail;
		}

		/* Reject malformed packets with non-null-terminated names. */
		vector<uint8_t> badTermData = infoData;
		badTermData.back() = 'X';

		ControlSerializer badTermDeserializer(ControlSerializer::Role::Worker);
		buffer = ByteStreamBuffer(const_cast<const uint8_t *>(badTermData.data()),
					  badTermData.size());
		if (!badTermDeserializer.deserialize<ControlInfoMap>(buffer).empty()) {
			cerr << "Control name without null terminator should be rejected" << endl;
			return TestFail;
		}

		/* Reject too-long names at serialization time. */
		vector<unique_ptr<ControlId>> longNameControlIds;
		ControlIdMap longNameIdMap;
		string longName(1025, 'n');

		longNameControlIds.emplace_back(std::make_unique<ControlId>(
			0x009a2002, longName, "v4l2", ControlTypeInteger32,
			ControlId::Direction::In));
		longNameIdMap.emplace(0x009a2002, longNameControlIds.back().get());

		ControlInfoMap::Map longNameInfo;
		longNameInfo.emplace(longNameControlIds.back().get(),
				     ControlInfo(ControlValue(int32_t{ 0 }),
						 ControlValue(int32_t{ 255 }),
						 ControlValue(int32_t{ 16 })));
		ControlInfoMap longNameInfoMap(std::move(longNameInfo), longNameIdMap);

		ControlSerializer longNameSerializer(ControlSerializer::Role::Proxy);
		size = longNameSerializer.binarySize(longNameInfoMap);
		infoData.resize(size);
		buffer = ByteStreamBuffer(infoData.data(), infoData.size());

		ret = longNameSerializer.serialize(longNameInfoMap, buffer);
		if (ret != -EINVAL) {
			cerr << "Too-long control name should fail serialization" << endl;
			return TestFail;
		}

		return TestPass;
	}
};

TEST_REGISTER(ControlSerializationTest)

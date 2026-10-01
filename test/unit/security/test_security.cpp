#include <security/xgl_security.h>

#include <gtest/gtest.h>

TEST(XglSecurityTest, ReplayWindowDoesNotTruncateSecuritySequence) {
    xgl_replay_window_t window = {};
    ASSERT_EQ(xgl_replay_window_init(&window, 1U, 2U, 3U, 64U), XGL_OK);
    EXPECT_TRUE(xgl_replay_window_accept(&window, 1U, 2U, 3U, 1U));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 1U, 2U, 3U,
                                         (UINT64_C(1) << 32U) + 1U));
}

TEST(XglSecurityTest, ReplayWindowAcceptsNewPacketsAndRejectsDuplicates) {
    xgl_replay_window_t window = {};
    ASSERT_EQ(xgl_replay_window_init(&window, 0x1234, 0x01020304U, 7, 64),
              XGL_OK);

    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 11));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 8, 12));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x9999, 0x01020304U, 7, 12));
}

TEST(XglSecurityTest, ReplayWindowClassifiesDuplicateWithinWindow) {
    xgl_replay_window_t window = {};
    ASSERT_EQ(xgl_replay_window_init(&window, 0x1234, 0x01020304U, 7, 64),
              XGL_OK);

    EXPECT_EQ(xgl_replay_window_check(&window, 0x1234, 0x01020304U, 7, 10),
              XGL_REPLAY_ACCEPT_NEW);
    EXPECT_EQ(xgl_replay_window_check(&window, 0x1234, 0x01020304U, 7, 10),
              XGL_REPLAY_ACCEPT_DUPLICATE);
    EXPECT_EQ(xgl_replay_window_check(&window, 0x1234, 0x01020304U, 7, 11),
              XGL_REPLAY_ACCEPT_NEW);
    EXPECT_EQ(xgl_replay_window_check(&window, 0x1234, 0x01020304U, 8, 12),
              XGL_REPLAY_REJECT);
}

TEST(XglSecurityTest, ReplayWindowRejectsPacketsOlderThanWindow) {
    xgl_replay_window_t window = {};
    ASSERT_EQ(xgl_replay_window_init(&window, 0x1234, 0x01020304U, 7, 64),
              XGL_OK);

    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 80));

    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 79));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 79));
}

TEST(XglSecurityTest, ReplayWindowValidatesParameters) {
    xgl_replay_window_t window = {};

    EXPECT_EQ(xgl_replay_window_init(nullptr, 1, 1, 1, 64),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_replay_window_init(&window, 0, 1, 1, 64),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_replay_window_init(&window, 1, 1, 1, 0),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_replay_window_init(&window, 1, 1, 1, 65),
              XGL_ERR_INVALID_PARAM);
    EXPECT_FALSE(xgl_replay_window_accept(nullptr, 1, 1, 1, 1));
}

#include <array>
#include <cstring>
#include <vector>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

#include "test_security_helpers.h"

namespace {
struct SecurityProviderSpy {
    size_t sign_calls = 0;
    size_t verify_calls = 0;
    bool fail_sign = false;
    bool wrong_tag_length = false;
    bool fail_verify = false;
    xgl_security_ctx_t* reenter = nullptr;
    xgl_error_t reenter_result = XGL_OK;
    uint64_t last_sequence = 0;
    std::array<uint8_t, XGL_AUTH_NONCE_SIZE> nonce = {};
    std::vector<uint8_t> aad;
};

/* Deterministic test digest only; this is not a cryptographic provider. */
static uint32_t security_test_digest(const xgl_auth_input_t* input) {
    uint32_t result = input->key_id;
    for (uint8_t byte : input->nonce) {
        result = (result * 33U) ^ byte;
    }
    for (size_t i = 0; i < input->aad_len; ++i) {
        result = (result * 33U) ^ input->aad[i];
    }
    for (size_t i = 0; i < input->payload_len; ++i) {
        result = (result * 33U) ^ input->payload[i];
    }
    return result;
}

static xgl_error_t security_test_sign(const xgl_auth_input_t* input,
                                      uint8_t* tag, size_t capacity,
                                      size_t* length, void* user) {
    auto* spy = static_cast<SecurityProviderSpy*>(user);
    spy->sign_calls++;
    spy->last_sequence = input->security_seq;
    std::memcpy(spy->nonce.data(), input->nonce, XGL_AUTH_NONCE_SIZE);
    spy->aad.assign(input->aad, input->aad + input->aad_len);
    EXPECT_EQ(input->version, XGL_AUTH_INPUT_VERSION);
    EXPECT_EQ(input->wire_version, 3U);
    if (spy->reenter != nullptr) {
        spy->reenter_result =
            xgl_security_session_close(spy->reenter, 2, 11, 9);
    }
    if (spy->fail_sign) {
        return XGL_ERR_BUSY;
    }
    if (capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    xgb_serialize_u32_le(tag, security_test_digest(input));
    *length = spy->wrong_tag_length ? 3U : 4U;
    return XGL_OK;
}

static xgl_error_t security_test_verify(const xgl_auth_input_t* input,
                                        const uint8_t* tag, size_t length,
                                        bool* valid, void* user) {
    auto* spy = static_cast<SecurityProviderSpy*>(user);
    spy->verify_calls++;
    *valid = !spy->fail_verify && length == 4U &&
             xgb_deserialize_u32_le(tag) == security_test_digest(input);
    return XGL_OK;
}

class XglSecuritySessionTest : public ::testing::Test {
  protected:
    SecurityProviderSpy spy;
    xgl_auth_provider_t provider = {security_test_sign, security_test_verify, 4,
                                    &spy};
    xgl_security_ctx_t sender = {};
    xgl_security_ctx_t receiver = {};
    xgl_security_session_config_t tx_config = {};
    xgl_security_session_config_t rx_config = {};
    xgl_frame_t frame = {};
    uint8_t extension[XGL_SESSION_EXT_SIZE] = {};
    const uint8_t payload[3] = {0x12, 0x34, 0x56};
    uint8_t bytes[128] = {};
    size_t length = 0;

    void SetUp() override {
        ASSERT_EQ(xgl_security_init(&sender, 1, true, &provider), XGL_OK);
        ASSERT_EQ(xgl_security_init(&receiver, 2, true, &provider), XGL_OK);
        tx_config = test_session_config(2, 11, 9);
        tx_config.tx_nonce_prefix = 0x01020304U;
        tx_config.rx_nonce_prefix = 0xA0B0C0D0U;
        rx_config = test_session_config(1, 11, 9, 7, true);
        rx_config.rx_nonce_prefix = tx_config.tx_nonce_prefix;
        rx_config.tx_nonce_prefix = tx_config.rx_nonce_prefix;
        uint8_t value[XGL_SESSION_EXT_VALUE_SIZE] = {};
        size_t written = 0;
        ASSERT_EQ(xgl_wire_encode_session_ext_value(value, sizeof(value), 9, 0,
                                                    &written),
                  XGL_OK);
        ASSERT_EQ(xgl_wire_encode_ext(extension, sizeof(extension),
                                      XGL_WIRE_EXT_SESSION, value, written,
                                      &written),
                  XGL_OK);
        xgl_frame_params_t params = {};
        params.source_id = 1;
        params.target_id = 2;
        params.connection_id = 11;
        params.packet_number = 17;
        params.reliable = true;
        params.ttl = 8;
        params.extensions = extension;
        params.extensions_len = sizeof(extension);
        params.payload = payload;
        params.payload_len = sizeof(payload);
        ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    }

    void Install() {
        ASSERT_EQ(xgl_security_session_install(&sender, &tx_config), XGL_OK);
        ASSERT_EQ(xgl_security_session_install(&receiver, &rx_config), XGL_OK);
    }

    xgl_error_t Sign() {
        return xgl_security_serialize_frame(bytes, sizeof(bytes), &frame,
                                            &sender, &length);
    }

    xgl_error_t Verify() {
        xgl_wire_frame_view_t view = {};
        xgl_error_t err = xgl_wire_decode_frame(&view, bytes, length, nullptr);
        return err == XGL_OK ? xgl_security_verify_frame(&receiver, &view)
                             : err;
    }

    void RecomputeCrc() {
        xgb_serialize_u16_le(bytes + length - 2,
                             xgcrc_crc16_modbus(bytes, length - 2));
    }
};
}  // namespace

TEST_F(XglSecuritySessionTest, RefusesUninstalledSendAndReceiveSessions) {
    EXPECT_EQ(Sign(), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(spy.sign_calls, 0U);
    ASSERT_EQ(xgl_security_session_install(&sender, &tx_config), XGL_OK);
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(spy.verify_calls, 0U);
    ASSERT_EQ(xgl_security_session_install(&receiver, &rx_config), XGL_OK);
    EXPECT_EQ(Verify(), XGL_OK);
}

TEST_F(XglSecuritySessionTest, ProducesFixedNonceAndCanonicalAadVector) {
    tx_config.tx_initial_seq = UINT64_C(0x0102030405060708);
    rx_config.rx_min_seq = tx_config.tx_initial_seq;
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    const std::array<uint8_t, 12> expected = {1, 2, 3, 4, 1, 2,
                                              3, 4, 5, 6, 7, 8};
    EXPECT_EQ(spy.nonce, expected);
    EXPECT_EQ(spy.last_sequence, tx_config.tx_initial_seq);
    ASSERT_EQ(spy.aad.size(), bytes[3]);
    const std::vector<uint8_t> expected_aad = {
        0xA5, 0x5A, 0x03, 0x35, 0x01, 0x13, 0x00, 0x40, 0x01, 0x00, 0x02,
        0x00, 0x0B, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x01, 0x0C, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0D, 0x07, 0x00, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x04};
    EXPECT_EQ(spy.aad, expected_aad);
    EXPECT_EQ(xgb_deserialize_u32_le(bytes + 16), 17U);
    EXPECT_EQ(Verify(), XGL_OK);
}

TEST_F(XglSecuritySessionTest,
       RetransmissionPreservesDataNumberWithFreshSecuritySequence) {
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(Verify(), XGL_OK);
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(spy.last_sequence, 1U);
    EXPECT_EQ(xgb_deserialize_u32_le(bytes + 16), 17U);
    EXPECT_EQ(Verify(), XGL_OK);
}

TEST_F(XglSecuritySessionTest,
       DataAckAndControlShareSecurityCounterButNotOrdering) {
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(Verify(), XGL_OK);
    frame.header.packet_type = XGL_PACKET_TYPE_ACK;
    frame.header.packet_number = 0;
    ASSERT_EQ(Sign(), XGL_OK);
    /* This ACK is intentionally lost. It is not a reliable DATA sequence. */
    frame.header.packet_type = XGL_PACKET_TYPE_CONTROL;
    ASSERT_EQ(Sign(), XGL_OK);
    frame.header.packet_type = XGL_PACKET_TYPE_DATA;
    frame.header.packet_number = 18;
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(spy.last_sequence, 3U);
    EXPECT_EQ(xgb_deserialize_u32_le(bytes + 16), 18U);
    EXPECT_EQ(Verify(), XGL_OK);
}

TEST_F(XglSecuritySessionTest, ProviderFailureAndWrongLengthConsumeSequences) {
    Install();
    spy.fail_sign = true;
    EXPECT_EQ(Sign(), XGL_ERR_BUSY);
    spy.fail_sign = false;
    spy.wrong_tag_length = true;
    EXPECT_EQ(Sign(), XGL_ERR_INVALID_FRAME);
    spy.wrong_tag_length = false;
    EXPECT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(spy.last_sequence, 2U);
    EXPECT_EQ(Verify(), XGL_OK);
}

TEST_F(XglSecuritySessionTest, CapacityFailureDoesNotCallProvider) {
    Install();
    EXPECT_EQ(xgl_security_serialize_frame(bytes, 55, &frame, &sender, &length),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(spy.sign_calls, 0U);
    EXPECT_EQ(sender.sessions[0].tx_next_seq, 0U);
}

TEST_F(XglSecuritySessionTest, MaximumSequenceIsUsedOnceAndNeverWraps) {
    tx_config.tx_initial_seq = UINT64_MAX;
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(spy.last_sequence, UINT64_MAX);
    EXPECT_EQ(Verify(), XGL_OK);
    EXPECT_EQ(Sign(), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(spy.sign_calls, 1U);
}

TEST_F(XglSecuritySessionTest, InvalidTagDoesNotAdvanceReplayWindow) {
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    bytes[bytes[3]] ^= 1U;
    RecomputeCrc();
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
    EXPECT_FALSE(receiver.sessions[0].replay.has_largest);
    bytes[bytes[3]] ^= 1U;
    RecomputeCrc();
    EXPECT_EQ(Verify(), XGL_OK);
}

TEST_F(XglSecuritySessionTest,
       TrustedReceiveFloorAndUnknownEpochDoNotCallProvider) {
    rx_config.rx_min_seq = 1;
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(spy.verify_calls, 0U);
    ASSERT_EQ(Sign(), XGL_OK);
    /* Epoch is first SESSION value, after the extension header. */
    bytes[XGL_WIRE_BASE_HEADER_SIZE + 2] = 10;
    RecomputeCrc();
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(spy.verify_calls, 0U);
}

TEST_F(XglSecuritySessionTest,
       DirectionalPrefixAndIdentityAreBoundToAuthentication) {
    rx_config.rx_nonce_prefix ^= 1U;
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
    EXPECT_FALSE(receiver.sessions[0].replay.has_largest);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, bytes, length, nullptr), XGL_OK);
    EXPECT_EQ(xgl_security_verify_frame(&sender, &view), XGL_ERR_INVALID_FRAME);
}

TEST_F(XglSecuritySessionTest,
       ForwardedTtlIsMutableButTrafficClassIsAuthenticated) {
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, bytes, length), XGL_OK);
    header.ttl--;
    ASSERT_EQ(xgl_wire_encode_header(bytes, length, &header), XGL_OK);
    RecomputeCrc();
    EXPECT_EQ(Verify(), XGL_OK);
    ASSERT_EQ(Sign(), XGL_OK);
    ASSERT_EQ(xgl_wire_decode_header(&header, bytes, length), XGL_OK);
    header.traffic_class ^= 0x20U;
    ASSERT_EQ(xgl_wire_encode_header(bytes, length, &header), XGL_OK);
    RecomputeCrc();
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
}

TEST_F(XglSecuritySessionTest,
       ClosedAndExistingNonceDomainsCannotBeReinstalled) {
    Install();
    EXPECT_EQ(xgl_security_session_install(&sender, &tx_config),
              XGL_ERR_INVALID_PARAM);
    ASSERT_EQ(xgl_security_session_close(&sender, 2, 11, 9), XGL_OK);
    EXPECT_EQ(Sign(), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_security_session_install(&sender, &tx_config),
              XGL_ERR_INVALID_PARAM);
    tx_config.session_epoch++;
    EXPECT_EQ(xgl_security_session_install(&sender, &tx_config),
              XGL_ERR_INVALID_PARAM);
    tx_config.tx_nonce_prefix += 10;
    tx_config.rx_nonce_prefix += 10;
    EXPECT_EQ(xgl_security_session_install(&sender, &tx_config), XGL_OK);
}

TEST_F(XglSecuritySessionTest, SameKeySameDirectionalPrefixIsRejected) {
    tx_config.rx_nonce_prefix = tx_config.tx_nonce_prefix;
    EXPECT_EQ(xgl_security_session_install(&sender, &tx_config),
              XGL_ERR_INVALID_PARAM);
}

TEST_F(XglSecuritySessionTest, ProviderCannotCloseOrReplaceActiveState) {
    Install();
    spy.reenter = &sender;
    EXPECT_EQ(Sign(), XGL_OK);
    EXPECT_EQ(spy.reenter_result, XGL_ERR_BUSY);
    EXPECT_TRUE(sender.sessions[0].active);
}

TEST_F(XglSecuritySessionTest,
       CapacityIsBoundedWithoutEvictingTrustedSessions) {
    for (uint32_t i = 0; i < XGL_SECURITY_SESSION_CAPACITY; ++i) {
        auto config = tx_config;
        config.session_epoch += i;
        config.tx_nonce_prefix = i * 2;
        config.rx_nonce_prefix = i * 2 + 1;
        ASSERT_EQ(xgl_security_session_install(&sender, &config), XGL_OK);
    }
    tx_config.session_epoch += XGL_SECURITY_SESSION_CAPACITY;
    EXPECT_EQ(xgl_security_session_install(&sender, &tx_config),
              XGL_ERR_NO_MEMORY);
}

TEST_F(XglSecuritySessionTest, OppositeDirectionsHaveDistinctNonceDomains) {
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    auto forward_nonce = spy.nonce;
    ASSERT_EQ(Verify(), XGL_OK);
    frame.header.source_id = 2;
    frame.header.target_id = 1;
    ASSERT_EQ(xgl_security_serialize_frame(bytes, sizeof(bytes), &frame,
                                           &receiver, &length),
              XGL_OK);
    EXPECT_EQ(spy.last_sequence, 0U);
    EXPECT_NE(spy.nonce, forward_nonce);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, bytes, length, nullptr), XGL_OK);
    EXPECT_EQ(xgl_security_verify_frame(&sender, &view), XGL_OK);
}

TEST_F(XglSecuritySessionTest, SecurityExtensionCannotBeSuppliedTwice) {
    Install();
    uint8_t extension_with_security[XGL_SESSION_EXT_SIZE +
                                    XGL_SECURITY_EXT_SIZE] = {};
    std::memcpy(extension_with_security, extension, sizeof(extension));
    uint8_t value[13] = {};
    size_t written = 0;
    ASSERT_EQ(xgl_wire_encode_security_ext_value(value, sizeof(value), 7, 0, 4,
                                                 &written),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(extension_with_security + sizeof(extension),
                                  XGL_SECURITY_EXT_SIZE, XGL_WIRE_EXT_SECURITY,
                                  value, written, &written),
              XGL_OK);
    frame.extensions = extension_with_security;
    frame.extensions_len = sizeof(extension_with_security);
    EXPECT_EQ(Sign(), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(spy.sign_calls, 0U);
    frame.extensions_len = SIZE_MAX;
    EXPECT_EQ(Sign(), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(spy.sign_calls, 0U);
}

TEST_F(XglSecuritySessionTest,
       PacketClassCannotBeChangedWithoutAuthentication) {
    Install();
    ASSERT_EQ(Sign(), XGL_OK);
    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, bytes, length), XGL_OK);
    header.packet_type = XGL_PACKET_TYPE_CONTROL;
    header.packet_number = 0;
    ASSERT_EQ(xgl_wire_encode_header(bytes, length, &header), XGL_OK);
    RecomputeCrc();
    EXPECT_EQ(Verify(), XGL_ERR_INVALID_FRAME);
}

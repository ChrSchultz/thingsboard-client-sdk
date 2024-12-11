#ifndef OTA_Firmware_Update_h
#define OTA_Firmware_Update_h

// Local includes.
#include "Attribute_Request.h"
#include "Shared_Attribute_Update.h"
#include "OTA_Handler.h"
#include "IAPI_Implementation.h"


uint8_t constexpr MAX_FW_TOPIC_SIZE = 33U;
uint8_t constexpr OTA_ATTRIBUTE_KEYS_AMOUNT = 5U;
char constexpr NO_FW_REQUEST_RESPONSE[] = "Did not receive requested shared attribute firmware keys. Ensure keys exist and device is connected";
// Firmware topics.
char constexpr FIRMWARE_RESPONSE_TOPIC[] = "v2/fw/response/%u/chunk/";
char constexpr FIRMWARE_RESPONSE_SUBSCRIBE_TOPIC[] = "v2/fw/response/+";
char constexpr FIRMWARE_REQUEST_TOPIC[] = "v2/fw/request/%u/chunk/%u";
// Firmware data keys.
char constexpr CURR_FW_TITLE_KEY[] = "current_fw_title";
char constexpr CURR_FW_VER_KEY[] = "current_fw_version";
char constexpr FW_ERROR_KEY[] = "fw_error";
char constexpr FW_STATE_KEY[] = "fw_state";
char constexpr FW_VER_KEY[] = "fw_version";
char constexpr FW_TITLE_KEY[] = "fw_title";
char constexpr FW_CHKS_KEY[] = "fw_checksum";
char constexpr FW_CHKS_ALGO_KEY[] = "fw_checksum_algorithm";
char constexpr FW_SIZE_KEY[] = "fw_size";
char constexpr CHECKSUM_AGORITM_MD5[] = "MD5";
char constexpr CHECKSUM_AGORITM_SHA256[] = "SHA256";
char constexpr CHECKSUM_AGORITM_SHA384[] = "SHA384";
char constexpr CHECKSUM_AGORITM_SHA512[] = "SHA512";
// Log messages.
char constexpr NUMBER_PRINTF[] = "%u";
char constexpr NO_FW[] = "Missing shared attribute firmware keys. Ensure you assigned an OTA update with binary";
char constexpr EMPTY_FW[] = "Received shared attribute firmware keys were NULL";
char constexpr FW_NOT_FOR_US[] = "Received firmware title (%s) is different and not meant for this device (%s)";
char constexpr FW_CHKS_ALGO_NOT_SUPPORTED[] = "Received checksum algorithm (%s) is not supported";
char constexpr NOT_ENOUGH_RAM[] = "Temporary allocating more internal client buffer failed, decrease OTA chunk size or decrease overall heap usage";
char constexpr RESETTING_FAILED[] = "Preparing for OTA firmware updates failed, attributes might be NULL";
#if THINGSBOARD_ENABLE_DEBUG
char constexpr PAGE_BREAK[] = "=================================";
char constexpr NEW_FW[] = "A new Firmware is available:";
char constexpr FROM_TOO[] = "(%s) => (%s)";
char constexpr DOWNLOADING_FW[] = "Attempting to download over MQTT...";
#endif // THINGSBOARD_ENABLE_DEBUG


/// @brief Handles the internal implementation of the ThingsBoard over the air firmware update API.
/// See https://thingsboard.io/docs/user-guide/ota-updates/ for more information
/// @tparam Logger Implementation that should be used to print error messages generated by internal processes and additional debugging messages if THINGSBOARD_ENABLE_DEBUG is set, default = DefaultLogger
template <typename Logger = DefaultLogger>
class OTA_Firmware_Update : public IAPI_Implementation {
  public:
    /// @brief Constructor
    OTA_Firmware_Update()
      : m_subscribe_api_callback()
      , m_send_json_callback()
      , m_send_json_string_callback()
      , m_subscribe_topic_callback()
      , m_unsubscribe_topic_callback()
      , m_get_receive_size_callback()
      , m_get_send_size_callback()
      , m_set_buffer_size_callback()
      , m_get_request_id_callback()
      , m_fw_callback()
      , m_previous_buffer_size(0U)
      , m_changed_buffer_size(false)
#if THINGSBOARD_ENABLE_STL
      , m_ota(std::bind(&OTA_Firmware_Update::Publish_Chunk_Request, this, std::placeholders::_1, std::placeholders::_2), std::bind(&OTA_Firmware_Update::Firmware_Send_State, this, std::placeholders::_1, std::placeholders::_2), std::bind(&OTA_Firmware_Update::Firmware_OTA_Unsubscribe, this))
#else
      , m_ota(OTA_Firmware_Update::staticPublishChunk, OTA_Firmware_Update::staticFirmwareSend, OTA_Firmware_Update::staticUnsubscribe)
#endif // THINGSBOARD_ENABLE_STL
      , m_response_topic()
      , m_fw_attribute_update()
      , m_fw_attribute_request()
    {
        // Can be ignored, because the topic is set correctly once we start an update anyway, therefore we simply insert 0 as the request id for now.
        // It just has to be set to an actual value that is not an empty string, because that would make the internal callback receive all other responses from the server as well,
        // even if they are not meant for this class and we are not currently updating the device
        (void)snprintf(m_response_topic, sizeof(m_response_topic), FIRMWARE_RESPONSE_TOPIC, 0U);
#if !THINGSBOARD_ENABLE_STL
        m_subscribedInstance = nullptr;
#endif // !THINGSBOARD_ENABLE_STL
    }

    /// @brief Checks if firmware settings are assigned to the connected device and if they are attempts to use those settings to start a firmware update.
    /// Will only be checked once and if there is no firmware assigned or if the assigned firmware is already installed this method will not update.
    /// This firmware status is only checked once, meaning to recheck the status either call this method again or use the Subscribe_Firmware_Update method.
    /// to be automatically informed and start the update if firmware has been assigned and it is not already installed.
    /// See https://thingsboard.io/docs/user-guide/ota-updates/ for more information
    /// @param callback Callback method that will be called
    /// @return Whether subscribing the given callback was successful or not
    bool Start_Firmware_Update(OTA_Update_Callback const & callback) {
        if (!Prepare_Firmware_Settings(callback))  {
            Logger::printfln(RESETTING_FAILED);
            return false;
        }

        // Request the firmware information
        constexpr char const * array[OTA_ATTRIBUTE_KEYS_AMOUNT] = {FW_CHKS_KEY, FW_CHKS_ALGO_KEY, FW_SIZE_KEY, FW_TITLE_KEY, FW_VER_KEY};
#if THINGSBOARD_ENABLE_DYNAMIC
#if THINGSBOARD_ENABLE_STL
        const Attribute_Request_Callback fw_request_callback(std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1), callback.Get_Timeout(), std::bind(&OTA_Firmware_Update::Request_Timeout, this), array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Attribute_Request_Callback fw_request_callback(OTA_Firmware_Update::onStaticFirmwareReceived, callback.Get_Timeout(), OTA_Firmware_Update::onStaticRequestTimeout, array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif // THINGSBOARD_ENABLE_STL
#else
#if THINGSBOARD_ENABLE_STL
        const Attribute_Request_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_request_callback(std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1), callback.Get_Timeout(), std::bind(&OTA_Firmware_Update::Request_Timeout, this), array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Attribute_Request_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_request_callback(OTA_Firmware_Update::onStaticFirmwareReceived, callback.Get_Timeout(), OTA_Firmware_Update::onStaticRequestTimeout, array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif // THINGSBOARD_ENABLE_STL
#endif //THINGSBOARD_ENABLE_DYNAMIC
        return m_fw_attribute_request.Shared_Attributes_Request(fw_request_callback);
    }

    /// @brief Stops the currently ongoing firmware update, calls the subscribed user finish callback with a failure if any update was stopped.
    /// See https://thingsboard.io/docs/user-guide/ota-updates/ for more information
    void Stop_Firmware_Update() {
        m_ota.Stop_Firmware_Update();
    }

    /// @brief Subscribes to any changes of the assigned firmware information on the connected device,
    /// meaning once we subscribed if we register any changes we will start the update if the given firmware is not already installed.
    /// Unlike Start_Firmware_Update this method only registers changes to the firmware information,
    /// meaning if the change occured while this device was asleep or turned off we will not update,
    /// to achieve that, it is instead recommended to call the Start_Firmware_Update method when the device has started once to check for that edge case.
    /// See https://thingsboard.io/docs/user-guide/ota-updates/ for more information
    /// @param callback Callback method that will be called
    /// @return Whether subscribing the given callback was successful or not
    bool Subscribe_Firmware_Update(OTA_Update_Callback const & callback) {
        if (!Prepare_Firmware_Settings(callback))  {
            Logger::printfln(RESETTING_FAILED);
            return false;
        }

        // Subscribes to changes of the firmware information
        char const * array[OTA_ATTRIBUTE_KEYS_AMOUNT] = {FW_CHKS_KEY, FW_CHKS_ALGO_KEY, FW_SIZE_KEY, FW_TITLE_KEY, FW_VER_KEY};
#if THINGSBOARD_ENABLE_DYNAMIC
#if THINGSBOARD_ENABLE_STL
        const Shared_Attribute_Callback fw_update_callback(std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1), array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Shared_Attribute_Callback fw_update_callback(OTA_Firmware_Update::onStaticFirmwareReceived, array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif // THINGSBOARD_ENABLE_STL
#else
#if THINGSBOARD_ENABLE_STL
        const Shared_Attribute_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_update_callback(std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1), array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Shared_Attribute_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_update_callback(OTA_Firmware_Update::onStaticFirmwareReceived, array + 0U, array + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif // THINGSBOARD_ENABLE_STL
#endif //THINGSBOARD_ENABLE_DYNAMIC
        return m_fw_attribute_update.Shared_Attributes_Subscribe(fw_update_callback);
    }

    /// @brief Sends the given firmware title and firmware version to the cloud.
    /// See https://thingsboard.io/docs/user-guide/ota-updates/ for more information
    /// @param current_fw_title Current device firmware title
    /// @param current_fw_version Current device firmware version
    /// @return Whether sending the current device firmware information was successful or not
    bool Firmware_Send_Info(char const * current_fw_title, char const * current_fw_version) {
        StaticJsonDocument<JSON_OBJECT_SIZE(2)> current_firmware_info;
        current_firmware_info[CURR_FW_TITLE_KEY] = current_fw_title;
        current_firmware_info[CURR_FW_VER_KEY] = current_fw_version;
        return m_send_json_callback.Call_Callback(TELEMETRY_TOPIC, current_firmware_info, Helper::Measure_Json(current_firmware_info));
    }

    /// @brief Sends the given firmware state to the cloud.
    /// See https://thingsboard.io/docs/user-guide/ota-updates/ for more information
    /// @param current_fw_state Current firmware download state
    /// @param fw_error Firmware error message that describes the current firmware state,
    /// simply do not enter a value and the default value will be used which overwrites the firmware error messages, default = ""
    /// @return Whether sending the current firmware download state was successful or not
    bool Firmware_Send_State(char const * current_fw_state, char const * fw_error = "") {
        StaticJsonDocument<JSON_OBJECT_SIZE(2)> current_firmware_state;
        current_firmware_state[FW_ERROR_KEY] = fw_error;
        current_firmware_state[FW_STATE_KEY] = current_fw_state;
        return m_send_json_callback.Call_Callback(TELEMETRY_TOPIC, current_firmware_state, Helper::Measure_Json(current_firmware_state));
    }

    API_Process_Type Get_Process_Type() const override {
        return API_Process_Type::RAW;
    }

    void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
        size_t const & request_id = m_fw_callback.Get_Request_ID();
        char response_topic[Helper::detectSize(FIRMWARE_RESPONSE_TOPIC, request_id)] = {};
        (void)snprintf(response_topic, sizeof(response_topic), FIRMWARE_RESPONSE_TOPIC, request_id);
        size_t const chunk = Helper::parseRequestId(response_topic, topic);
        m_ota.Process_Firmware_Packet(chunk, payload, length);
    }

    void Process_Json_Response(char const * topic, JsonDocument const & data) override {
        // Nothing to do
    }

    bool Compare_Response_Topic(char const * topic) const override {
        return strncmp(m_response_topic, topic, strlen(m_response_topic)) == 0;
    }

    bool Unsubscribe() override {
        Stop_Firmware_Update();
        return true;
    }

    bool Resubscribe_Topic() override {
        return Firmware_OTA_Subscribe();
    }

#if !THINGSBOARD_USE_ESP_TIMER
    void loop() override {
        m_ota.update();
    }
#endif // !THINGSBOARD_USE_ESP_TIMER

    void Initialize() override {
        m_subscribe_api_callback.Call_Callback(m_fw_attribute_update);
        m_subscribe_api_callback.Call_Callback(m_fw_attribute_request);
    }

    void Set_Client_Callbacks(Callback<void, IAPI_Implementation &>::function subscribe_api_callback, Callback<bool, char const * const, JsonDocument const &, size_t const &>::function send_json_callback, Callback<bool, char const * const, char const * const>::function send_json_string_callback, Callback<bool, char const * const>::function subscribe_topic_callback, Callback<bool, char const * const>::function unsubscribe_topic_callback, Callback<uint16_t>::function get_receive_size_callback, Callback<uint16_t>::function get_send_size_callback, Callback<bool, uint16_t, uint16_t>::function set_buffer_size_callback, Callback<size_t *>::function get_request_id_callback) override {
        m_subscribe_api_callback.Set_Callback(subscribe_api_callback);
        m_send_json_callback.Set_Callback(send_json_callback);
        m_send_json_string_callback.Set_Callback(send_json_string_callback);
        m_subscribe_topic_callback.Set_Callback(subscribe_topic_callback);
        m_unsubscribe_topic_callback.Set_Callback(unsubscribe_topic_callback);
        m_get_receive_size_callback.Set_Callback(get_receive_size_callback);
        m_get_send_size_callback.Set_Callback(get_send_size_callback);
        m_set_buffer_size_callback.Set_Callback(set_buffer_size_callback);
        m_get_request_id_callback.Set_Callback(get_request_id_callback);
    }

  private:
    /// @brief Checks the included information in the callback,
    /// and attempts to sends the current device firmware information to the cloud
    /// @param callback Callback method that will be called
    /// @return Whether checking and sending the current device firmware information was successful or not
    bool Prepare_Firmware_Settings(OTA_Update_Callback const & callback) {
        char const * current_fw_title = callback.Get_Firmware_Title();
        char const * current_fw_version = callback.Get_Firmware_Version();

        if (Helper::stringIsNullorEmpty(current_fw_title) || Helper::stringIsNullorEmpty(current_fw_version)) {
            return false;
        }
        else if (!Firmware_Send_Info(current_fw_title, current_fw_version)) {
            return false;
        }

        size_t * p_request_id = m_get_request_id_callback.Call_Callback();
        if (p_request_id == nullptr) {
            Logger::printfln(REQUEST_ID_NULL);
            return false;
        }
        auto & request_id = *p_request_id;

        m_fw_callback = callback;
        m_fw_callback.Set_Request_ID(++request_id);
        (void)snprintf(m_response_topic, sizeof(m_response_topic), FIRMWARE_RESPONSE_TOPIC, request_id);
        return true;
    }

    /// @brief Subscribes to the firmware response topic
    /// @return Whether subscribing to the firmware response topic was successful or not
    bool Firmware_OTA_Subscribe() {
        if (!m_subscribe_topic_callback.Call_Callback(FIRMWARE_RESPONSE_SUBSCRIBE_TOPIC)) {
            char message[strlen(SUBSCRIBE_TOPIC_FAILED) + strlen(FIRMWARE_RESPONSE_SUBSCRIBE_TOPIC) + 2] = {};
            (void)snprintf(message, sizeof(message), SUBSCRIBE_TOPIC_FAILED, FIRMWARE_RESPONSE_SUBSCRIBE_TOPIC);
            Logger::printfln(message);
            Firmware_Send_State(FW_STATE_FAILED, message);
            return false;
        }
        return true;
    }

    /// @brief Unsubscribes from the firmware response topic and clears any memory associated with the firmware update,
    /// should not be called before actually fully completing the firmware update.
    /// @return Whether unsubscribing from the firmware response topic was successful or not
    bool Firmware_OTA_Unsubscribe() {
        // Buffer size has been set to another value before the update,
        // to allow to receive ota chunck packets that might be much bigger than the normal
        // buffer size would allow, therefore we return to the previous value to decrease overall memory usage
        if (m_changed_buffer_size) {
            (void)m_set_buffer_size_callback.Call_Callback(m_previous_buffer_size, m_get_send_size_callback.Call_Callback());
        }
        // Reset now not needed private member variables
        m_fw_callback = OTA_Update_Callback();
        // Unsubscribe from the topic
        return m_unsubscribe_topic_callback.Call_Callback(FIRMWARE_RESPONSE_SUBSCRIBE_TOPIC);
    }

    /// @brief Publishes a request for the given firmware chunk
    /// @param request_id Request ID corresponding to the extact OTA update package we want to request chunks from
    /// @param request_chunck Chunk index that should be requested from the server
    /// @return Whether publishing the message was successful or not
    bool Publish_Chunk_Request(size_t const & request_id, size_t const & request_chunck) {
        // Calculate the number of chuncks we need to request,
        // in order to download the complete firmware binary
        uint16_t const & chunk_size = m_fw_callback.Get_Chunk_Size();

        // Convert the interger size into a readable string
        char size[Helper::detectSize(NUMBER_PRINTF, chunk_size)] = {};
        (void)snprintf(size, sizeof(size), NUMBER_PRINTF, chunk_size);

        char topic[Helper::detectSize(FIRMWARE_REQUEST_TOPIC, request_id, request_chunck)] = {};
        (void)snprintf(topic, sizeof(topic), FIRMWARE_REQUEST_TOPIC, request_id, request_chunck);
        return m_send_json_string_callback.Call_Callback(topic, size);
    }

    /// @brief Handler if the firmware shared attribute request times out without getting a response.
    /// Is used to signal that the update could not be started, because the current firmware information could not be fetched
    void Request_Timeout() {
        Logger::printfln(NO_FW_REQUEST_RESPONSE);
        Firmware_Send_State(FW_STATE_FAILED, NO_FW_REQUEST_RESPONSE);
    }

    /// @brief Callback that will be called upon firmware shared attribute arrival
    /// @param data Json data containing key-value pairs for the needed firmware information,
    /// to ensure we have a firmware assigned and can start the update over MQTT
    void Firmware_Shared_Attribute_Received(JsonObjectConst const & data) {
        // Check if firmware is available for our device
        if (!data.containsKey(FW_VER_KEY) || !data.containsKey(FW_TITLE_KEY) || !data.containsKey(FW_CHKS_KEY) || !data.containsKey(FW_CHKS_ALGO_KEY) || !data.containsKey(FW_SIZE_KEY)) {
            Logger::printfln(NO_FW);
            Firmware_Send_State(FW_STATE_FAILED, NO_FW);
            return;
        }

        char const * fw_title = data[FW_TITLE_KEY];
        char const * fw_version = data[FW_VER_KEY];
        char const * fw_checksum = data[FW_CHKS_KEY];
        char const * fw_algorithm = data[FW_CHKS_ALGO_KEY];
        size_t const fw_size = data[FW_SIZE_KEY];

        char const * curr_fw_title = m_fw_callback.Get_Firmware_Title();
        char const * curr_fw_version = m_fw_callback.Get_Firmware_Version();

        if (fw_title == nullptr || fw_version == nullptr || curr_fw_title == nullptr || curr_fw_version == nullptr || fw_algorithm == nullptr || fw_checksum == nullptr) {
            Logger::printfln(EMPTY_FW);
            Firmware_Send_State(FW_STATE_FAILED, EMPTY_FW);
            return;
        }
        // If firmware version and title is the same, we do not initiate an update, because we expect the type of binary to be the same one we are currently using
        // and therefore updating would be useless as we have already updated previously
        else if (strncmp(curr_fw_title, fw_title, strlen(curr_fw_title)) == 0 && strncmp(curr_fw_version, fw_version, strlen(curr_fw_version)) == 0) {
            Firmware_Send_State(FW_STATE_UPDATED);
            return;
        }
        // If firmware title is not the same, we do not initiate an update, because we expect the binary to be for another type of device
        // and downloading it on this device could possibly cause hardware issues or even destroy the device
        else if (strncmp(curr_fw_title, fw_title, strlen(curr_fw_title)) != 0) {
            char message[strlen(FW_NOT_FOR_US) + strlen(fw_title) + strlen(curr_fw_title) + 3] = {};
            (void)snprintf(message, sizeof(message), FW_NOT_FOR_US, fw_title, curr_fw_title);
            Logger::printfln(message);
            Firmware_Send_State(FW_STATE_FAILED, message);
            return;
        }

        mbedtls_md_type_t fw_checksum_algorithm = mbedtls_md_type_t{};

        if (strncmp(CHECKSUM_AGORITM_MD5, fw_algorithm, strlen(CHECKSUM_AGORITM_MD5)) == 0) {
            fw_checksum_algorithm = mbedtls_md_type_t::MBEDTLS_MD_MD5;
        }
        else if (strncmp(CHECKSUM_AGORITM_SHA256, fw_algorithm, strlen(CHECKSUM_AGORITM_SHA256)) == 0) {
            fw_checksum_algorithm = mbedtls_md_type_t::MBEDTLS_MD_SHA256;
        }
        else if (strncmp(CHECKSUM_AGORITM_SHA384, fw_algorithm, strlen(CHECKSUM_AGORITM_SHA384)) == 0) {
            fw_checksum_algorithm = mbedtls_md_type_t::MBEDTLS_MD_SHA384;
        }
        else if (strncmp(CHECKSUM_AGORITM_SHA512, fw_algorithm, strlen(CHECKSUM_AGORITM_SHA512)) == 0) {
            fw_checksum_algorithm = mbedtls_md_type_t::MBEDTLS_MD_SHA512;
        }
        else {
            char message[strlen(FW_CHKS_ALGO_NOT_SUPPORTED) + strlen(fw_algorithm) + 2] = {};
            (void)snprintf(message, sizeof(message), FW_CHKS_ALGO_NOT_SUPPORTED, fw_algorithm);
            Logger::printfln(message);
            Firmware_Send_State(FW_STATE_FAILED, message);
            return;
        }

        m_fw_callback.Call_Update_Starting_Callback();
        bool const result = Firmware_OTA_Subscribe();
        if (!result) {
            m_fw_callback.Call_Callback(result);
            return;
        }

#if THINGSBOARD_ENABLE_DEBUG
        Logger::printfln(PAGE_BREAK);
        Logger::printfln(NEW_FW);
        char firmware[strlen(FROM_TOO) + strlen(curr_fw_version) + strlen(fw_version) + 3] = {};
        (void)snprintf(firmware, sizeof(firmware), FROM_TOO, curr_fw_version, fw_version);
        Logger::printfln(firmware);
        Logger::printfln(DOWNLOADING_FW);
#endif // THINGSBOARD_ENABLE_DEBUG

        // Calculate the number of chuncks we need to request,
        // in order to download the complete firmware binary
        const uint16_t& chunk_size = m_fw_callback.Get_Chunk_Size();

        // Get the previous buffer size and cache it so the previous settings can be restored.
        m_previous_buffer_size = m_get_receive_size_callback.Call_Callback();
        m_changed_buffer_size = m_previous_buffer_size < (chunk_size + 50U);

        // Increase size of receive buffer
        if (m_changed_buffer_size && !m_set_buffer_size_callback.Call_Callback(chunk_size + 50U, m_get_send_size_callback.Call_Callback())) {
            Logger::printfln(NOT_ENOUGH_RAM);
            Firmware_Send_State(FW_STATE_FAILED, NOT_ENOUGH_RAM);
            m_fw_callback.Call_Callback(false);
            return;
        }

        m_ota.Start_Firmware_Update(m_fw_callback, fw_size, fw_checksum, fw_checksum_algorithm);
    }

#if !THINGSBOARD_ENABLE_STL
    static void onStaticFirmwareReceived(JsonDocument const & data) {
        if (m_subscribedInstance == nullptr) {
            return;
        }
        m_subscribedInstance->Firmware_Shared_Attribute_Received(data);
    }

    static void onStaticRequestTimeout() {
        if (m_subscribedInstance == nullptr) {
            return;
        }
        m_subscribedInstance->Request_Timeout();
    }

    static bool staticPublishChunk(size_t const & request_id, size_t const & request_chunck) {
        if (m_subscribedInstance == nullptr) {
            return false;
        }
        return m_subscribedInstance->Publish_Chunk_Request(request_id, request_chunck);
    }

    static bool staticFirmwareSend(char const * current_fw_state, char const * fw_error = nullptr) {
        if (m_subscribedInstance == nullptr) {
            return false;
        }
        return m_subscribedInstance->Firmware_Send_State(current_fw_state, fw_error);
    }

    static bool staticUnsubscribe() {
        if (m_subscribedInstance == nullptr) {
            return false;
        }
        return m_subscribedInstance->Firmware_OTA_Unsubscribe();
    }

    // Used API Implementation cannot call a instanced method when message arrives on subscribed topic.
    // Only free-standing function is allowed.
    // To be able to forward event to an instance, rather than to a function, this pointer exists.
    static OTA_Firmware_Update                                               *m_subscribedInstance;
#endif // !THINGSBOARD_ENABLE_STL

    Callback<void, IAPI_Implementation &>                                    m_subscribe_api_callback = {};            // Subscribe additional api callback
    Callback<bool, char const * const, JsonDocument const &, size_t const &> m_send_json_callback = {};                // Send json document callback
    Callback<bool, char const * const, char const * const>                   m_send_json_string_callback = {};         // Send json string callback
    Callback<bool, char const * const>                                       m_subscribe_topic_callback = {};          // Subscribe mqtt topic client callback
    Callback<bool, char const * const>                                       m_unsubscribe_topic_callback = {};        // Unubscribe mqtt topic client callback
    Callback<uint16_t>                                                       m_get_receive_size_callback = {};         // Get client receive buffer size callback
    Callback<uint16_t>                                                       m_get_send_size_callback = {};            // Get client send buffer size callback
    Callback<bool, uint16_t, uint16_t>                                       m_set_buffer_size_callback = {};          // Set client buffer size callback
    Callback<size_t *>                                                       m_get_request_id_callback = {};           // Get internal request id callback

    OTA_Update_Callback                                                      m_fw_callback = {};                       // OTA update response callback
    uint16_t                                                                 m_previous_buffer_size = {};              // Previous buffer size of the underlying client, used to revert to the previously configured buffer size if it was temporarily increased by the OTA update
    bool                                                                     m_changed_buffer_size = {};               // Whether the buffer size had to be changed, because the previous internal buffer size was to small to hold the firmware chunks
    OTA_Handler<Logger>                                                      m_ota = {};                               // Class instance that handles the flashing and creating a hash from the given received binary firmware data
    char                                                                     m_response_topic[MAX_FW_TOPIC_SIZE] = {}; // Firmware response topic that contains the specific request ID of the firmware we actually want to download
#if !THINGSBOARD_ENABLE_DYNAMIC
    Shared_Attribute_Update<1U, OTA_ATTRIBUTE_KEYS_AMOUNT, Logger>           m_fw_attribute_update = {};               // API implementation to be informed if needed fw attributes have been updated
    Attribute_Request<1U, OTA_ATTRIBUTE_KEYS_AMOUNT, Logger>                 m_fw_attribute_request = {};              // API implementation to request the needed fw attributes to start updating
#else
    Shared_Attribute_Update<Logger>                                          m_fw_attribute_update = {};               // API implementation to be informed if needed fw attributes have been updated
    Attribute_Request<Logger>                                                m_fw_attribute_request = {};              // API implementation to request the needed fw attributes to start updating
#endif // !THINGSBOARD_ENABLE_DYNAMIC
};

#if !THINGSBOARD_ENABLE_STL
OTA_Firmware_Update *OTA_Firmware_Update::m_subscribedInstance = nullptr;
#endif

#endif // OTA_Firmware_Update_h
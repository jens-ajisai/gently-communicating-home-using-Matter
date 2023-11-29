#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/attribute-storage.h>

using namespace ::chip::app::Clusters;

static constexpr uint8_t kDefaultDynamicEndpointVersion = 1;
static constexpr uint8_t kNodeLabelSize = 32;
static constexpr uint8_t kDescriptorAttributeArraySize = 254;

namespace {

enum MatterDeviceTypes : uint16_t {
  Orphan = 0xf001,
  Rootdevice = 0x0016,
  Powersource = 0x0011,
  Otarequestor = 0x0012,
  Otaprovider = 0x0014,
  Aggregator = 0x000e,
  Bridgeddevice = 0x0013,
  Onofflight = 0x0100,
  Dimmablelight = 0x0101,
  Colortemperaturelight = 0x010c,
  Extendedcolorlight = 0x010d,
  Onoffpluginunit = 0x010a,
  Dimmablepluginunit = 0x010b,
  Pump = 0x0303,
  Onofflightswitch = 0x0103,
  Dimmerswitch = 0x0104,
  Colordimmerswitch = 0x0105,
  Controlbridge = 0x0840,
  Pumpcontroller = 0x0304,
  Genericswitch = 0x000f,
  Contactsensor = 0x0015,
  Lightsensor = 0x0106,
  Occupancysensor = 0x0107,
  Tempsensor = 0x0302,
  Pressuresensor = 0x0305,
  Flowsensor = 0x0306,
  Humiditysensor = 0x0307,
  Onoffsensor = 0x0850,
  Doorlock = 0x000a,
  Doorlockcontroller = 0x000b,
  Windowcovering = 0x0202,
  Windowcoveringcontroller = 0x0203,
  Heatcool = 0x0300,
  Thermostat = 0x0301,
  Fan = 0x002b,
  CastingVideoplayer = 0x0023,
  BasicVideoplayer = 0x0028,
  CastingVideoclient = 0x0029,
  VideoRemotecontrol = 0x002a,
  Speaker = 0x0022,
  Contentapp = 0x0024,
  Modeselect = 0x0027,
  AllClustersApp = 0x0000,
  SecondaryNetworkCommissioning = 0xf002
};

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY,
                          kDescriptorAttributeArraySize, 0), /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY,
                              kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY,
                              kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY,
                              kDescriptorAttributeArraySize, 0), /* parts list */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic Information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING,
                          kNodeLabelSize, 0), /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1,
                              0), /* Reachable */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::FeatureMap::Id, BITMAP32,
                              4, 0), /* feature map */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0), /* on/off */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId onOffIncomingCommands[] = {
    OnOff::Commands::Off::Id,
    OnOff::Commands::On::Id,
    OnOff::Commands::Toggle::Id,
    OnOff::Commands::OffWithEffect::Id,
    OnOff::Commands::OnWithRecallGlobalScene::Id,
    OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::CurrentLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::RemainingTime::Id, INT16U, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MinLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::MaxLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::OnLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::Options::Id, BITMAP8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::StartUpCurrentLevel::Id, INT8U, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::FeatureMap::Id, BITMAP32, 4,
                              0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId levelControlCommands[] = {
    LevelControl::Commands::MoveToLevel::Id,
    LevelControl::Commands::Move::Id,
    LevelControl::Commands::Step::Id,
    LevelControl::Commands::Stop::Id,
    LevelControl::Commands::MoveToLevelWithOnOff::Id,
    LevelControl::Commands::MoveWithOnOff::Id,
    LevelControl::Commands::StepWithOnOff::Id,
    LevelControl::Commands::StopWithOnOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedPostureClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(LevelControl::Id, levelControlAttrs, nullptr, levelControlCommands),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, nullptr,
                            nullptr),
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(bridgedPostureEndpoint, bridgedPostureClusters);


static constexpr EmberAfDeviceType bridgedPostureDeviceTypes[] = {
    {static_cast<chip::DeviceTypeId>(MatterDeviceTypes::Dimmerswitch),
     kDefaultDynamicEndpointVersion},
    {static_cast<chip::DeviceTypeId>(MatterDeviceTypes::Bridgeddevice),
     kDefaultDynamicEndpointVersion}};

}; /* namespace */

DataVersion postureDataVersions[ArraySize(bridgedPostureClusters)];

auto deviceTypes = chip::Span<const EmberAfDeviceType>(bridgedPostureDeviceTypes);
auto dataVersions = chip::Span<chip::DataVersion>(postureDataVersions);

#define BT_UUID_PCS_VAL BT_UUID_128_ENCODE(0xe5130001, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS BT_UUID_DECLARE_128(BT_UUID_PCS_VAL)

#define BT_UUID_PCS_SCORE_MIN_VAL \
  BT_UUID_128_ENCODE(0xe5130003, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS_SCORE_MEA_VAL \
  BT_UUID_128_ENCODE(0xe5130004, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS_SCORE_MAX_VAL \
  BT_UUID_128_ENCODE(0xe5130005, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)

#define BT_UUID_PCS_SCORE_MIN BT_UUID_DECLARE_128(BT_UUID_PCS_SCORE_MIN_VAL)
#define BT_UUID_PCS_SCORE_MEA BT_UUID_DECLARE_128(BT_UUID_PCS_SCORE_MEA_VAL)
#define BT_UUID_PCS_SCORE_MAX BT_UUID_DECLARE_128(BT_UUID_PCS_SCORE_MAX_VAL)

#include <map>

BLEConnectivityManager::DeviceFilter deviceFilter{
    .type = BLEConnectivityManager::DeviceFilter::FILTER_TYPE_UUID,
    .filter = {.serviceUuid = BT_UUID_PCS}};

std::map<std::pair<ClusterId, AttributeId>, std::pair<MatterDevice::AttributeTypes, bt_uuid *>>
    matterBleMapping{
        {std::make_pair(LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id),
         std::make_pair(MatterDevice::AttributeTypes::SUBSCRIBE, BT_UUID_PCS_SCORE_MEA)},
        {std::make_pair(LevelControl::Id, LevelControl::Attributes::MinLevel::Id),
         std::make_pair(MatterDevice::AttributeTypes::READ_ONCE, BT_UUID_PCS_SCORE_MIN)},
        {std::make_pair(LevelControl::Id, LevelControl::Attributes::MaxLevel::Id),
         std::make_pair(MatterDevice::AttributeTypes::READ_ONCE, BT_UUID_PCS_SCORE_MAX)}};

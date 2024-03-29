/*
// Copyright (c) 2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "redfish_util.hpp"

#include <app.hpp>
#include <sdbusplus/asio/property.hpp>

#include <array>

namespace redfish
{

static constexpr std::array<std::string_view, 1> ledGroupInterface = {
    "xyz.openbmc_project.Led.Group"};
/**
 * @brief Retrieves identify led group properties over dbus
 *
 * @param[in] aResp     Shared pointer for generating response message.
 *
 * @return None.
 */
// TODO (Gunnar): Remove IndicatorLED after enough time has passed
inline void
    getIndicatorLedState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG << "Get led groups";
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.LED.GroupManager",
        "/xyz/openbmc_project/led/groups/enclosure_identify_blink",
        "xyz.openbmc_project.Led.Group", "Asserted",
        [aResp](const boost::system::error_code ec, const bool blinking) {
        // Some systems may not have enclosure_identify_blink object so
        // proceed to get enclosure_identify state.
        if (ec == boost::system::errc::invalid_argument)
        {
            BMCWEB_LOG_ERROR
                << "Get identity blinking LED failed, missmatch in property type";
            messages::internalError(aResp->res);
            return;
        }

        // Blinking ON, no need to check enclosure_identify assert.
        if (!ec && blinking)
        {
            aResp->res.jsonValue["IndicatorLED"] = "Blinking";
            return;
        }

        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus,
            "xyz.openbmc_project.LED.GroupManager",
            "/xyz/openbmc_project/led/groups/enclosure_identify",
            "xyz.openbmc_project.Led.Group", "Asserted",
            [aResp](const boost::system::error_code ec2, const bool ledOn) {
            if (ec2 == boost::system::errc::invalid_argument)
            {
                BMCWEB_LOG_ERROR
                    << "Get enclosure identity led failed, missmatch in property type";
                messages::internalError(aResp->res);
                return;
            }

            if (ec2)
            {
                return;
            }

            if (ledOn)
            {
                aResp->res.jsonValue["IndicatorLED"] = "Lit";
            }
            else
            {
                aResp->res.jsonValue["IndicatorLED"] = "Off";
            }
        });
    });
}

/**
 * @brief Sets identify led group properties
 *
 * @param[in] aResp     Shared pointer for generating response message.
 * @param[in] ledState  LED state passed from request
 *
 * @return None.
 */
// TODO (Gunnar): Remove IndicatorLED after enough time has passed
inline void
    setIndicatorLedState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& ledState)
{
    BMCWEB_LOG_DEBUG << "Set led groups";
    bool ledOn = false;
    bool ledBlinkng = false;

    if (ledState == "Lit")
    {
        ledOn = true;
    }
    else if (ledState == "Blinking")
    {
        ledBlinkng = true;
    }
    else if (ledState != "Off")
    {
        messages::propertyValueNotInList(aResp->res, ledState, "IndicatorLED");
        return;
    }

    crow::connections::systemBus->async_method_call(
        [aResp, ledOn, ledBlinkng](const boost::system::error_code ec) mutable {
        if (ec)
        {
            // Some systems may not have enclosure_identify_blink object so
            // Lets set enclosure_identify state to true if Blinking is
            // true.
            if (ledBlinkng)
            {
                ledOn = true;
            }
        }
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec2) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec2;
                messages::internalError(aResp->res);
                return;
            }
            messages::success(aResp->res);
        },
            "xyz.openbmc_project.LED.GroupManager",
            "/xyz/openbmc_project/led/groups/enclosure_identify",
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Led.Group", "Asserted",
            dbus::utility::DbusVariantType(ledOn));
    },
        "xyz.openbmc_project.LED.GroupManager",
        "/xyz/openbmc_project/led/groups/enclosure_identify_blink",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Led.Group", "Asserted",
        dbus::utility::DbusVariantType(ledBlinkng));
}

inline void getLedAsset(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        const std::string& ledGroup,
                        const std::function<void(bool asserted)>& callback)
{
    dbus::utility::getDbusObject(
        ledGroup, ledGroupInterface,
        [aResp, ledGroup,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& object) {
        if (ec || object.empty())
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec.message();
            messages::internalError(aResp->res);
            return;
        }

        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, object.begin()->first, ledGroup,
            "xyz.openbmc_project.Led.Group", "Asserted",
            [aResp, callback](const boost::system::error_code& ec1,
                              bool assert) {
            if (ec1)
            {
                if (ec1.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec1.value();
                    messages::internalError(aResp->res);
                }
                return;
            }

            callback(assert);
        });
    });
}

inline void setLedAsset(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        const std::string& ledGroup, bool ledState)
{
    dbus::utility::getDbusObject(
        ledGroup, ledGroupInterface,
        [aResp, ledGroup,
         ledState](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& object) {
        if (ec || object.empty())
        {
            BMCWEB_LOG_ERROR << "DBUS response error " << ec.message();
            messages::internalError(aResp->res);
            return;
        }

        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, object.begin()->first, ledGroup,
            "xyz.openbmc_project.Led.Group", "Asserted", ledState,
            [aResp](const boost::system::error_code& ec1) {
            if (ec1)
            {
                if (ec1.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR << "DBUS response error " << ec1.value();
                    messages::internalError(aResp->res);
                }
                return;
            }
        });
    });
}

/**
 * @brief Retrieves identify led group properties over dbus
 *
 * @param[in] aResp     Shared pointer for generating response message.
 * @param[in] objPath   Object path on PIM
 *
 * @return None.
 */
inline void getLocationIndicatorActive(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::function<void(bool asserted)>&& callback)
{
    BMCWEB_LOG_DEBUG << "Get LocationIndicatorActive";

    dbus::utility::getAssociationEndPoints(
        objPath + "/identifying",
        [aResp, callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperEndPoints& endpoints) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec.value();
                messages::internalError(aResp->res);
            }
            return;
        }

        for (const auto& endpoint : endpoints)
        {
            getLedAsset(aResp, endpoint, callback);
        }
    });
}

inline void
    getLocationIndicatorActive(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& objPath)
{
    getLocationIndicatorActive(aResp, objPath, [aResp](bool asserted) {
        aResp->res.jsonValue["LocationIndicatorActive"] = asserted;
    });
}

/**
 * @brief Sets identify led group properties
 *
 * @param[in] aResp     Shared pointer for generating response message.
 * @param[in] objPath   Object path on PIM
 * @param[in] ledState  LED state passed from request
 *
 * @return None.
 */
inline void
    setLocationIndicatorActive(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& objPath, bool ledState)
{
    BMCWEB_LOG_DEBUG << "Set LocationIndicatorActive";

    dbus::utility::getAssociationEndPoints(
        objPath + "/identifying",
        [aResp, ledState,
         objPath](const boost::system::error_code& ec,
                  const dbus::utility::MapperEndPoints& endpoints) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec.value();
                messages::internalError(aResp->res);
            }
            messages::resourceNotFound(aResp->res, "LedGroup", objPath);
            return;
        }

        for (const auto& endpoint : endpoints)
        {
            setLedAsset(aResp, endpoint, ledState);
        }
    });
}
} // namespace redfish

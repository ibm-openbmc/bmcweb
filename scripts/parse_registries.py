#!/usr/bin/env python3
import argparse
import json
import os

import requests

PRAGMA_ONCE = """#pragma once
"""

WARNING = """/****************************************************************
 *                 READ THIS WARNING FIRST
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined messages.
 * DO NOT modify this registry outside of running the
 * parse_registries.py script.  The definitions contained within
 * this file are owned by DMTF.  Any modifications to these files
 * should be first pushed to the relevant registry in the DMTF
 * github organization.
 ***************************************************************/"""

REGISTRY_HEADER = (
    PRAGMA_ONCE
    + WARNING
    + """
#include "registries.hpp"

#include <array>

// clang-format off

namespace redfish::registries::{}
{{
"""
)

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

include_path = os.path.realpath(
    os.path.join(SCRIPT_DIR, "..", "redfish-core", "include", "registries")
)

proxies = {"https": os.environ.get("https_proxy", None)}


def make_getter(dmtf_name, header_name, type_name):
    url = "https://redfish.dmtf.org/registries/{}".format(dmtf_name)
    dmtf = requests.get(url, proxies=proxies)
    dmtf.raise_for_status()
    json_file = json.loads(dmtf.text)
    path = os.path.join(include_path, header_name)
    return (path, json_file, type_name, url)


def openbmc_local_getter():
    url = ""
    with open(
        os.path.join(
            SCRIPT_DIR,
            "..",
            "redfish-core",
            "include",
            "registries",
            "openbmc.json",
        ),
        "rb",
    ) as json_file:
        json_file = json.load(json_file)

    path = os.path.join(include_path, "openbmc_message_registry.hpp")
    return (path, json_file, "openbmc", url)


def update_registries(files):
    # Remove the old files
    for file, json_dict, namespace, url in files:
        try:
            os.remove(file)
        except BaseException:
            print("{} not found".format(file))

        with open(file, "w") as registry:

            version_split = json_dict["RegistryVersion"].split(".")

            registry.write(REGISTRY_HEADER.format(namespace))
            # Parse the Registry header info
            registry.write(
                "const Header header = {{\n"
                '    "{json_dict[@Redfish.Copyright]}",\n'
                '    "{json_dict[@odata.type]}",\n'
                "    {version_split[0]},\n"
                "    {version_split[1]},\n"
                "    {version_split[2]},\n"
                '    "{json_dict[Name]}",\n'
                '    "{json_dict[Language]}",\n'
                '    "{json_dict[Description]}",\n'
                '    "{json_dict[RegistryPrefix]}",\n'
                '    "{json_dict[OwningEntity]}",\n'
                "}};\n"
                "constexpr const char* url =\n"
                '    "{url}";\n'
                "\n"
                "constexpr std::array registry =\n"
                "{{\n".format(
                    json_dict=json_dict,
                    url=url,
                    version_split=version_split,
                )
            )

            messages_sorted = sorted(json_dict["Messages"].items())
            for messageId, message in messages_sorted:
                registry.write(
                    "    MessageEntry{{\n"
                    '        "{messageId}",\n'
                    "        {{\n"
                    '            "{message[Description]}",\n'
                    '            "{message[Message]}",\n'
                    '            "{message[MessageSeverity]}",\n'
                    "            {message[NumberOfArgs]},\n"
                    "            {{".format(
                        messageId=messageId, message=message
                    )
                )
                paramTypes = message.get("ParamTypes")
                if paramTypes:
                    for paramType in paramTypes:
                        registry.write(
                            '\n                "{}",'.format(paramType)
                        )
                    registry.write("\n            },\n")
                else:
                    registry.write("},\n")
                registry.write(
                    '            "{message[Resolution]}",\n'
                    "        }}}},\n".format(message=message)
                )

            registry.write("\n};\n\nenum class Index\n{\n")
            for index, (messageId, message) in enumerate(messages_sorted):
                messageId = messageId[0].lower() + messageId[1:]
                registry.write("    {} = {},\n".format(messageId, index))
            registry.write(
                "}};\n}} // namespace redfish::registries::{}\n".format(
                    namespace
                )
            )


def get_privilege_string_from_list(privilege_list):
    privilege_string = "{{\n"
    for privilege_json in privilege_list:
        privileges = privilege_json["Privilege"]
        privilege_string += "    {"
        for privilege in privileges:
            if privilege == "NoAuth":
                continue
            privilege_string += '"'
            privilege_string += privilege
            privilege_string += '",\n'
        if privilege != "NoAuth":
            privilege_string = privilege_string[:-2]
        privilege_string += "}"
        privilege_string += ",\n"
    privilege_string = privilege_string[:-2]
    privilege_string += "\n}}"
    return privilege_string


def get_variable_name_for_privilege_set(privilege_list):
    names = []
    for privilege_json in privilege_list:
        privileges = privilege_json["Privilege"]
        names.append("And".join(privileges))
    return "Or".join(names)


def create_Subordinate_Overrides(target_list):
    target_list_create = []
    target_list_copy = target_list.copy()
    target_list_create.append([target_list[0]])
    targetLength = len(target_list)
    for i in range(targetLength):
        # remove firs element
        # it already get covered
        target_list_copy.pop(0)
        target_temp = []
        for j in range(i + 1):
            if not (j > 0 and j == (targetLength - 1)):
                target_temp.append(target_list[j])
        if target_list_copy:
            for target_end in target_list_copy:
                target_temp_copy = target_temp.copy()
                target_temp_copy.append(target_end)
                target_list_create.append(target_temp_copy)
    return target_list_create


PRIVILEGE_HEADER = (
    PRAGMA_ONCE
    + WARNING
    + """
#include "privileges.hpp"

#include <array>

// clang-format off

namespace redfish::privileges
{
"""
)


def make_privilege_registry():
    path, json_file, type_name, url = make_getter(
        "Redfish_1.5.0_PrivilegeRegistry.json",
        "privilege_registry.hpp",
        "privilege",
    )
    with open(path, "w") as registry:
        registry.write(PRIVILEGE_HEADER)

        privilege_dict = {}
        for mapping in json_file["Mappings"]:
            # first pass, identify all the unique privilege sets
            for operation, privilege_list in mapping["OperationMap"].items():
                privilege_dict[
                    get_privilege_string_from_list(privilege_list)
                ] = (privilege_list,)
        for index, key in enumerate(privilege_dict):
            (privilege_list,) = privilege_dict[key]
            name = get_variable_name_for_privilege_set(privilege_list)
            registry.write(
                "const std::array<Privileges, {length}> "
                "privilegeSet{name} = {key};\n".format(
                    length=len(privilege_list), name=name, key=key
                )
            )
            privilege_dict[key] = (privilege_list, name)

        for mapping in json_file["Mappings"]:
            entity = mapping["Entity"]
            registry.write("// {}\n".format(entity))
            for operation, privilege_list in mapping["OperationMap"].items():
                privilege_string = get_privilege_string_from_list(
                    privilege_list
                )
                operation = operation.lower()

                registry.write(
                    "const static auto& {}{} = privilegeSet{};\n".format(
                        operation, entity, privilege_dict[privilege_string][1]
                    )
                )
            registry.write("\n")
            if "SubordinateOverrides" in mapping:
                for subordinateOverrides in mapping["SubordinateOverrides"]:
                    target_list_list = create_Subordinate_Overrides(
                        subordinateOverrides["Targets"]
                    )
                    for target_list in target_list_list:
                        concateVarName = ""
                        registry.write("// Subordinate override for ")
                        for target in target_list:
                            registry.write(target + " -> ")
                            concateVarName += target
                        registry.write(entity)
                        registry.write("\n")
                        for operation, privilege_list in subordinateOverrides[
                            "OperationMap"
                        ].items():
                            privilege_string = get_privilege_string_from_list(
                                privilege_list
                            )
                            operation = operation.lower()
                            registry.write(
                                "const static auto& {}{}SubOver{} ="
                                "privilegeSet{};\n".format(
                                    operation,
                                    entity,
                                    concateVarName,
                                    privilege_dict[privilege_string][1],
                                )
                            )
                        registry.write("\n")
                registry.write("\n")
        registry.write(
            "} // namespace redfish::privileges\n// clang-format on\n"
        )


def to_pascal_case(text):
    s = text.replace("_", " ")
    s = s.split()
    if len(text) == 0:
        return text
    return "".join(i.capitalize() for i in s[0:])


def main():
    dmtf_registries = (
        ("base", "1.19.0"),
        ("composition", "1.1.2"),
        ("environmental", "1.0.1"),
        ("ethernet_fabric", "1.0.1"),
        ("fabric", "1.0.2"),
        ("heartbeat_event", "1.0.1"),
        ("job_event", "1.0.1"),
        ("license", "1.0.3"),
        ("log_service", "1.0.1"),
        ("network_device", "1.0.3"),
        ("platform", "1.0.1"),
        ("power", "1.0.1"),
        ("resource_event", "1.3.0"),
        ("sensor_event", "1.0.1"),
        ("storage_device", "1.2.1"),
        ("task_event", "1.0.3"),
        ("telemetry", "1.0.0"),
        ("update", "1.0.2"),
    )

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--registries",
        type=str,
        default="privilege,openbmc,"
        + ",".join([dmtf[0] for dmtf in dmtf_registries]),
        help="Comma delimited list of registries to update",
    )

    args = parser.parse_args()

    registries = set(args.registries.split(","))
    files = []

    for registry, version in dmtf_registries:
        if registry in registries:
            registry_pascal_case = to_pascal_case(registry)
            files.append(
                make_getter(
                    f"{registry_pascal_case}.{version}.json",
                    f"{registry}_message_registry.hpp",
                    registry,
                )
            )
    if "openbmc" in registries:
        files.append(openbmc_local_getter())

    update_registries(files)

    if "privilege" in registries:
        make_privilege_registry()


if __name__ == "__main__":
    main()

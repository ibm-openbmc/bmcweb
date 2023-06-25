#!/usr/bin/env python3
import requests
import zipfile
from io import BytesIO

import os
from collections import OrderedDict, defaultdict
import shutil
import json

import xml.etree.ElementTree as ET

VERSION = "DSP8010_2022.2"

WARNING = """/****************************************************************
 *                 READ THIS WARNING FIRST
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined schemas.
 * DO NOT modify this registry outside of running the
 * update_schemas.py script.  The definitions contained within
 * this file are owned by DMTF.  Any modifications to these files
 * should be first pushed to the relevant registry in the DMTF
 * github organization.
 ***************************************************************/"""

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

proxies = {"https": os.environ.get("https_proxy", None)}

r = requests.get(
    "https://www.dmtf.org/sites/default/files/standards/documents/"
    + VERSION
    + ".zip",
    proxies=proxies,
)

r.raise_for_status()


static_path = os.path.realpath(
    os.path.join(SCRIPT_DIR, "..", "static", "redfish", "v1")
)


cpp_path = os.path.realpath(
    os.path.join(SCRIPT_DIR, "..", "redfish-core", "include")
)


schema_path = os.path.join(static_path, "schema")
json_schema_path = os.path.join(static_path, "JsonSchemas")
metadata_index_path = os.path.join(static_path, "$metadata", "index.xml")

zipBytesIO = BytesIO(r.content)
zip_ref = zipfile.ZipFile(zipBytesIO)


class SchemaVersion:
    '''
    A Python class for sorting Redfish schema versions.  Allows sorting Redfish
    versions in the way humans expect, by comparing version strings as lists
    (ie 0_2_0 comes before 0_10_0) in the way humans expect.  It does case
    insensitive schema name comparisons
    '''

    def __init__(self, key):
        key = str.casefold(key)

        split_tup = key.split(".")
        self.version_pieces = [split_tup[0]]
        if len(split_tup) < 2:
            return
        version = split_tup[1]

        if version.startswith("v"):
            version = version[1:]
        if any(char.isdigit() for char in version):
            self.version_pieces.extend([int(x)
                                       for x in version.split("_")])

    def __lt__(self, other):
        return self.version_pieces < other.version_pieces


# Remove the old files
skip_prefixes = "Oem"
if os.path.exists(schema_path):
    files = [
        os.path.join(schema_path, f)
        for f in os.listdir(schema_path)
        if not f.startswith(skip_prefixes)
    ]
    for f in files:
        os.remove(f)
if os.path.exists(json_schema_path):
    files = [
        os.path.join(json_schema_path, f)
        for f in os.listdir(json_schema_path)
        if not f.startswith(skip_prefixes)
    ]
    for f in files:
        if os.path.isfile(f):
            os.remove(f)
        else:
            shutil.rmtree(f)
try:
    os.remove(metadata_index_path)
except FileNotFoundError:
    pass

if not os.path.exists(schema_path):
    os.makedirs(schema_path)
if not os.path.exists(json_schema_path):
    os.makedirs(json_schema_path)

csdl_filenames = []
json_schema_files = defaultdict(list)

for zip_file in zip_ref.infolist():
    if zip_file.is_dir():
        continue
    if zip_file.filename.startswith("csdl/"):
        csdl_filenames.append(os.path.basename(zip_file.filename))
    elif zip_file.filename.startswith("json-schema/"):
        filename = os.path.basename(zip_file.filename)
        filenamesplit = filename.split(".")
        json_schema_files[filenamesplit[0]].append(filename)
    elif zip_file.filename.startswith("openapi/"):
        pass
    elif zip_file.filename.startswith("dictionaries/"):
        pass

# sort the json files by version
for key, value in json_schema_files.items():
    value.sort(key=SchemaVersion, reverse=True)

# Create a dictionary ordered by schema name
json_schema_files = OrderedDict(
    sorted(json_schema_files.items(), key=lambda x: SchemaVersion(x[0]))
)

csdl_filenames.sort(key=SchemaVersion)
with open(metadata_index_path, "w") as metadata_index:

    metadata_index.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    metadata_index.write(
        "<edmx:Edmx xmlns:edmx="
        '"http://docs.oasis-open.org/odata/ns/edmx"'
        ' Version="4.0">\n'
    )

    for filename in csdl_filenames:
        # filename looks like Zone_v1.xml
        filenamesplit = filename.split("_")

        with open(os.path.join(schema_path, filename), "wb") as schema_out:

            metadata_index.write(
                '    <edmx:Reference Uri="/redfish/v1/schema/'
                + filename
                + '">\n'
            )

            content = zip_ref.read(os.path.join("csdl", filename))
            content = content.replace(b"\r\n", b"\n")
            xml_root = ET.fromstring(content)
            edmx = "{http://docs.oasis-open.org/odata/ns/edmx}"
            edm = "{http://docs.oasis-open.org/odata/ns/edm}"
            for edmx_child in xml_root:
                if edmx_child.tag == edmx + "DataServices":
                    for data_child in edmx_child:
                        if data_child.tag == edm + "Schema":
                            namespace = data_child.attrib["Namespace"]
                            if namespace.startswith("RedfishExtensions"):
                                metadata_index.write(
                                    "        "
                                    '<edmx:Include Namespace="'
                                    + namespace
                                    + '"  Alias="Redfish"/>\n'
                                )

                            else:
                                metadata_index.write(
                                    "        "
                                    '<edmx:Include Namespace="'
                                    + namespace
                                    + '"/>\n'
                                )
            schema_out.write(content)
            metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:DataServices>\n"
        "        <Schema "
        'xmlns="http://docs.oasis-open.org/odata/ns/edm" '
        'Namespace="Service">\n'
        '            <EntityContainer Name="Service" '
        'Extends="ServiceRoot.v1_0_0.ServiceContainer"/>\n'
        "        </Schema>\n"
        "    </edmx:DataServices>\n"
    )
    # TODO:Issue#32 There's a bug in the script that currently deletes this
    # schema (because it's an OEM schema). Because it's the only six, and we
    # don't update schemas very often, we just manually fix it. Need a
    # permanent fix to the script.
    metadata_index.write(
        '    <edmx:Reference Uri="/redfish/v1/schema/OemManager_v1.xml">\n'
    )
    metadata_index.write('        <edmx:Include Namespace="OemManager"/>\n')
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        '    <edmx:Reference Uri="'
        '/redfish/v1/schema/OemComputerSystem_v1.xml">\n'
    )
    metadata_index.write(
        '        <edmx:Include Namespace="OemComputerSystem"/>\n'
    )
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        '    <edmx:Reference Uri="'
        '/redfish/v1/schema/OemVirtualMedia_v1.xml">\n'
    )
    metadata_index.write(
        '        <edmx:Include Namespace="OemVirtualMedia"/>\n'
    )
    metadata_index.write(
        '        <edmx:Include Namespace="OemVirtualMedia.v1_0_0"/>\n'
    )
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        '    <edmx:Reference Uri="'
        '/redfish/v1/schema/OemAccountService_v1.xml">\n'
    )
    metadata_index.write(
        '        <edmx:Include Namespace="OemAccountService"/>\n'
    )
    metadata_index.write(
        '        <edmx:Include Namespace="OemAccountService.v1_0_0"/>\n'
    )
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemServiceRoot_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemServiceRoot\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        '    <edmx:Reference Uri="/redfish/v1/schema/OemSession_v1.xml">\n'
    )
    metadata_index.write('        <edmx:Include Namespace="OemSession"/>\n')
    metadata_index.write(
        '        <edmx:Include Namespace="OemSession.v1_0_0"/>\n'
    )
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\"/redfish/v1/schema/OemLogEntry_v1.xml\">\n")
    metadata_index.write("        <edmx:Include Namespace=\"OemLogEntry\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemLogEntry.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemLogEntryAttachment_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemLogEntryAttachment\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemManagerAccount.v1_0_0.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemManagerAccount\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemManagerAccount.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemUpdateService_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemUpdateService\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemUpdateService.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemAssembly_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemAssembly\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemAssembly.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemPowerSupplyMetrics_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemPowerSupplyMetrics\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemPowerSupplyMetrics.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemChassis_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemChassis\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemChassis.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "       <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemPCIeDevice_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemPCIeDevice\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemPCIeDevice.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemPCIeSlots_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemPCIeSlots\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemPCIeSlots.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemMessage_v1.xml\">\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemMessage\"/>\n")
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemMessage.v1_0_0\"/>\n")
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write(
        "    <edmx:Reference Uri=\""
        "/redfish/v1/schema/OemFabricAdapter_v1.xml\">\n"
    )
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemFabricAdapter\"/>\n"
    )
    metadata_index.write(
        "        <edmx:Include Namespace=\"OemFabricAdapter.v1_0_0\"/>\n"
    )
    metadata_index.write("    </edmx:Reference>\n")

    metadata_index.write("</edmx:Edmx>\n")


for schema, version in json_schema_files.items():
    zip_filepath = os.path.join("json-schema", version[0])
    schemadir = os.path.join(json_schema_path, schema)
    if not os.path.exists(schemadir):
        os.makedirs(schemadir)

    location_json = OrderedDict()
    location_json["Language"] = "en"
    location_json["PublicationUri"] = (
        "http://redfish.dmtf.org/schemas/v1/" + schema + ".json")
    location_json["Uri"] = (
        "/redfish/v1/JsonSchemas/" + schema + "/" + schema + ".json")

    index_json = OrderedDict()
    index_json["@odata.context"] = \
        "/redfish/v1/$metadata#JsonSchemaFile.JsonSchemaFile"
    index_json["@odata.id"] = "/redfish/v1/JsonSchemas/" + schema
    index_json["@odata.type"] = "#JsonSchemaFile.v1_0_2.JsonSchemaFile"
    index_json["Name"] = schema + " Schema File"
    index_json["Schema"] = "#" + schema + "." + schema
    index_json["Description"] = schema + " Schema File Location"
    index_json["Id"] = schema
    index_json["Languages"] = ["en"]
    index_json["Languages@odata.count"] = 1
    index_json["Location"] = [location_json]
    index_json["Location@odata.count"] = 1

    with open(os.path.join(schemadir, "index.json"), "w") as schema_file:
        json.dump(index_json, schema_file, indent=4)
    with open(os.path.join(schemadir, schema + ".json"), "wb") as schema_file:
        schema_file.write(zip_ref.read(zip_filepath).replace(b"\r\n", b"\n"))

with open(os.path.join(json_schema_path, "index.json"), 'w') as index_file:
    members = [{"@odata.id": "/redfish/v1/JsonSchemas/" + schema}
               for schema in json_schema_files]

    members.sort(key=lambda x: x["@odata.id"])

    indexData = OrderedDict()

    indexData["@odata.id"] = "/redfish/v1/JsonSchemas"
    indexData["@odata.context"] = ("/redfish/v1/$metadata"
                                   "#JsonSchemaFileCollection."
                                   "JsonSchemaFileCollection")
    indexData["@odata.type"] = ("#JsonSchemaFileCollection."
                                "JsonSchemaFileCollection")
    indexData["Name"] = "JsonSchemaFile Collection"
    indexData["Description"] = "Collection of JsonSchemaFiles"
    indexData["Members@odata.count"] = len(json_schema_files)
    indexData["Members"] = members

    json.dump(indexData, index_file, indent=2)

zip_ref.close()

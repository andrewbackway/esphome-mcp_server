import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@andrew-backway"]
DEPENDENCIES = ["network"]
AUTO_LOAD = []

CONF_AUTO_DISCOVER = "auto_discover"
CONF_EXPOSE_SCRIPTS = "expose_scripts"
CONF_ENTITY_TYPES = "entity_types"

mcp_server_ns = cg.esphome_ns.namespace("mcp_server")
MCPServerComponent = mcp_server_ns.class_("MCPServerComponent", cg.Component)

ALL_ENTITY_TYPES = [
    "sensor",
    "binary_sensor",
    "switch",
    "light",
    "fan",
    "cover",
    "climate",
    "text_sensor",
    "number",
    "select",
    "lock",
    "button",
    "media_player",
    "alarm_control_panel",
    "event",
    "valve",
    "update",
    "date",
    "time",
    "datetime",
    "text",
]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MCPServerComponent),
        cv.Optional(CONF_PORT, default=8080): cv.port,
        cv.Optional(CONF_AUTO_DISCOVER, default=True): cv.boolean,
        cv.Optional(CONF_EXPOSE_SCRIPTS, default=True): cv.boolean,
        cv.Optional(CONF_ENTITY_TYPES): cv.ensure_list(
            cv.one_of(*ALL_ENTITY_TYPES, lower=True)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(40.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_auto_discover(config[CONF_AUTO_DISCOVER]))
    cg.add(var.set_expose_scripts(config[CONF_EXPOSE_SCRIPTS]))

    if CONF_ENTITY_TYPES in config:
        for etype in config[CONF_ENTITY_TYPES]:
            cg.add(var.add_entity_type_filter(etype))
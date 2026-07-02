from dataclasses import dataclass
import os


@dataclass(frozen=True)
class ToolSettings:
    host: str = "127.0.0.1"
    port: int = 18081


def get_settings() -> ToolSettings:
    host = os.getenv("BRIDGE_TOOLS_HOST", "127.0.0.1")
    port_text = os.getenv("BRIDGE_TOOLS_PORT", "18081")
    return ToolSettings(host=host, port=int(port_text))

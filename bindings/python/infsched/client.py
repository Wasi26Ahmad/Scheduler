import socket
import json
import urllib.request
from typing import Dict, Any, Optional
from .types import Tensor, DataType, Request

class Client:
    """Client for connecting to the infsched daemon over Unix socket or HTTP ops."""
    def __init__(self, endpoint: str = "http://127.0.0.1:9090"):
        self.endpoint = endpoint.rstrip("/")

    def ready(self) -> Dict[str, Any]:
        """Check scheduler readiness and active isolation level."""
        url = f"{self.endpoint}/readyz"
        try:
            with urllib.request.urlopen(url, timeout=3) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except Exception as e:
            return {"ready": False, "error": str(e)}

    def health(self) -> Dict[str, Any]:
        """Check daemon health."""
        url = f"{self.endpoint}/healthz"
        try:
            with urllib.request.urlopen(url, timeout=3) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except Exception as e:
            return {"status": "unhealthy", "error": str(e)}

    def metrics(self) -> str:
        """Fetch Prometheus metrics."""
        url = f"{self.endpoint}/metrics"
        with urllib.request.urlopen(url, timeout=3) as resp:
            return resp.read().decode("utf-8")


class Engine:
    """In-process scheduler engine for low-latency standalone inference without daemon IPC overhead."""
    def __init__(self, config_path: str = "configs/infsched.yaml"):
        self.config_path = config_path

    def infer(self, model: str, inputs: Dict[str, Any], deadline_ms: int = 100) -> Dict[str, Any]:
        """Perform in-process inference."""
        return {
            "model": model,
            "status": "ok",
            "outputs": inputs
        }

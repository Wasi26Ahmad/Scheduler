from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Any

class DataType(Enum):
    FLOAT32 = "float32"
    INT8 = "int8"
    INT32 = "int32"
    INT64 = "int64"
    FLOAT16 = "float16"

class AdmissionResult(Enum):
    ACCEPTED = "Accepted"
    QUEUE_FULL = "QueueFull"
    OVER_QUOTA = "OverQuota"
    MODEL_NOT_FOUND = "ModelNotFound"
    DRAINING = "Draining"
    DEADLINE_ALREADY_MISSED = "DeadlineAlreadyMissed"

@dataclass
class Tensor:
    name: str
    shape: List[int]
    dtype: DataType = DataType.FLOAT32
    data: Optional[bytes] = None

@dataclass
class Request:
    model: str
    inputs: List[Tensor] = field(default_factory=list)
    tenant: str = "default"
    deadline_ms: int = 100
    priority: int = 0

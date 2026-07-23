from dataclasses import dataclass
from typing import Optional


IMAGE_NOT_FOUND = "IMAGE_NOT_FOUND"
CONFIG_NOT_FOUND = "CONFIG_NOT_FOUND"
CONFIG_INVALID = "CONFIG_INVALID"
INPUT_INVALID = "INPUT_INVALID"
TASK_CONFLICT = "TASK_CONFLICT"
UNKNOWN_ERROR = "UNKNOWN_ERROR"


@dataclass
class M1Failure(Exception):
    code: str
    message: str
    details: Optional[dict] = None
    recoverable: bool = True

    def as_dict(self):
        return {
            "code": self.code,
            "message": self.message,
            "module": "m1_input_config",
            "recoverable": self.recoverable,
            "details": self.details or {},
        }

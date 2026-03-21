#!/usr/bin/env python
"""Health check endpoint"""

from datetime import datetime
from typing import Dict, Any
from fastapi import APIRouter, Query
from pydantic import BaseModel

router = APIRouter()


class HealthResponse(BaseModel):
    status: str
    timestamp: str
    version: str
    services: Dict[str, str]


@router.get("/health", response_model=HealthResponse)
async def health_check():
    """Check API health status"""
    return HealthResponse(
        status="healthy",
        timestamp=datetime.now().isoformat(),
        version="1.0.0",
        services={
            "api": "ok",
            "database": "ok"
        }
    )


@router.get("/ping")
async def ping():
    """Simple ping endpoint"""
    return {"message": "pong", "timestamp": datetime.now().isoformat()}

#!/usr/bin/env python
"""
Trading-Python API Service
FastAPI server providing signals and sentiment data to Trading-CPP

Run with: uv run python scripts/run_service.py
"""

import os
import sys
from datetime import datetime
from typing import List, Optional, Dict, Any

from fastapi import FastAPI, Query, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import logging

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from src.api.routes import signals, sentiment, health, models

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Create FastAPI app
app = FastAPI(
    title="Trading-Python API",
    description="API service providing signals and sentiment data for Trading-CPP",
    version="1.0.0"
)

# Add CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Include routers
app.include_router(health.router, prefix="/api", tags=["health"])
app.include_router(signals.router, prefix="/api", tags=["signals"])
app.include_router(sentiment.router, prefix="/api", tags=["sentiment"])
app.include_router(models.router, prefix="/api/models", tags=["models"])


@app.on_event("startup")
async def startup_event():
    """Initialize services on startup"""
    logger.info("Starting Trading-Python API Service...")
    logger.info(f"Server time: {datetime.now().isoformat()}")

    # Initialize data directory
    data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), "data")
    os.makedirs(data_dir, exist_ok=True)

    # Run startup ticker selection and model training
    try:
        from src.service.scheduler import get_scheduler
        scheduler = get_scheduler()
        result = scheduler.select_and_train_tickers()

        if result.get("status") == "success":
            logger.info(f"Startup ticker selection: {result['tickers']}")
            logger.info(f"Models trained: {result['trained']}")
        elif result.get("status") == "skipped":
            logger.info(f"Startup ticker selection skipped: {result['reason']}")
        else:
            logger.warning(f"Startup ticker selection issue: {result}")

    except Exception as e:
        logger.error(f"Startup ticker selection failed: {e}")
        logger.warning("Continuing without ticker selection - will retry at scheduled time")

    logger.info("API Service ready")


@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup on shutdown"""
    logger.info("Shutting down Trading-Python API Service...")


# Root endpoint
@app.get("/")
async def root():
    """Root endpoint"""
    return {
        "service": "Trading-Python API",
        "version": "1.0.0",
        "status": "running",
        "endpoints": {
            "health": "/api/health",
            "signals": "/api/signals/{ticker}",
            "batch_signals": "/api/batch/signals",
            "sentiment": "/api/sentiment/{ticker}",
            "models": "/api/models/status",
            "select_tickers": "/api/models/select-tickers"
        }
    }


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

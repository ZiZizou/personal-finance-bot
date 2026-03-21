#!/usr/bin/env python
"""Model management endpoints for dynamic ONNX models"""

import os
import sys
from datetime import datetime
from typing import List, Dict, Any
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

logger = None


def get_logger():
    global logger
    if logger is None:
        import logging
        logging.basicConfig(level=logging.INFO)
        logger = logging.getLogger(__name__)
    return logger


router = APIRouter()


# Request/Response models
class SelectTickersRequest(BaseModel):
    tickers: List[str]


class SelectTickersResponse(BaseModel):
    status: str
    tickers: List[str]
    trained: List[str]
    removed: List[str] = []
    message: str


class ModelStatusResponse(BaseModel):
    active_models: int
    max_models: int
    tickers: List[str]
    last_update: str = None


@router.post("/select-tickers", response_model=SelectTickersResponse)
async def select_tickers(request: SelectTickersRequest):
    """
    Set the selected tickers for dynamic ONNX model training.

    C++ calls this endpoint to report which tickers should have active models.
    The ticker manager will train models for new tickers and clean up unused ones.
    """
    log = get_logger()

    if not request.tickers:
        raise HTTPException(status_code=400, detail="No tickers provided")

    if len(request.tickers) > 20:
        raise HTTPException(status_code=400, detail="Maximum 20 tickers allowed")

    log.info(f"Received ticker selection request: {request.tickers}")

    try:
        from src.service.ticker_manager import get_ticker_manager
        manager = get_ticker_manager()

        result = manager.set_selected_tickers(request.tickers)

        return SelectTickersResponse(**result)

    except Exception as e:
        log.error(f"Error selecting tickers: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/status", response_model=ModelStatusResponse)
async def get_model_status():
    """
    Get the status of active ONNX models.

    Returns information about which tickers have active models.
    """
    log = get_logger()

    try:
        from src.service.ticker_manager import get_ticker_manager
        manager = get_ticker_manager()

        status = manager.get_model_status()

        return ModelStatusResponse(
            active_models=status['active_models'],
            max_models=status['max_models'],
            tickers=status['tickers'],
            last_update=status.get('last_update')
        )

    except Exception as e:
        log.error(f"Error getting model status: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/signal/{ticker}")
async def get_model_signal(ticker: str):
    """
    Get trading signal from the ONNX model for a specific ticker.

    Returns the signal if a model exists for the ticker, otherwise 404.
    """
    log = get_logger()
    ticker = ticker.upper()

    try:
        from src.service.ticker_manager import get_ticker_manager
        manager = get_ticker_manager()

        signal = manager.get_signal(ticker)

        if signal is None:
            raise HTTPException(
                status_code=404,
                detail=f"No model available for {ticker}"
            )

        return signal

    except HTTPException:
        raise
    except Exception as e:
        log.error(f"Error getting signal for {ticker}: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/trigger-training")
async def trigger_training(tickers: List[str]):
    """
    Manually trigger model training for specified tickers.

    This is an alternative to the automatic selection via C++.
    """
    log = get_logger()

    if not tickers:
        raise HTTPException(status_code=400, detail="No tickers provided")

    try:
        from src.service.ticker_manager import get_ticker_manager
        manager = get_ticker_manager()

        # Set tickers and trigger training
        result = manager.set_selected_tickers(tickers)

        return result

    except Exception as e:
        log.error(f"Error triggering training: {e}")
        raise HTTPException(status_code=500, detail=str(e))

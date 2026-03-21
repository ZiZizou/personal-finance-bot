"""
Reinforcement Learning trading agent training.
Uses Stable-Baselines3 for PPO/SAC agents.
"""

import os
import sys
import argparse
from datetime import datetime
from typing import Optional

import numpy as np
import pandas as pd
import gymnasium as gym
from gymnasium import spaces
from stable_baselines3 import PPO, SAC, A2C
from stable_baselines3.common.callbacks import BaseCallback
from stable_baselines3.common.monitor import Monitor


class TradingEnv(gym.Env):
    """
    Custom trading environment for RL.
    Based on OpenAI Gymnasium interface.
    """

    metadata = {'render_modes': ['human']}

    def __init__(
        self,
        df: pd.DataFrame,
        initial_balance: float = 100000,
        commission: float = 0.001,
        window_size: int = 20
    ):
        super().__init__()

        self.df = df.reset_index(drop=True)
        self.initial_balance = initial_balance
        self.commission = commission
        self.window_size = window_size

        # Calculate feature dimensions
        self.n_features = len(df.columns)

        # Action space: 0 = hold, 1 = buy, 2 = sell
        self.action_space = spaces.Discrete(3)

        # Observation space: balance + position + price history + features
        obs_dim = 2 + self.n_features * window_size
        self.observation_space = spaces.Box(
            low=-np.inf,
            high=np.inf,
            shape=(obs_dim,),
            dtype=np.float32
        )

        # Initialize state
        self.reset()

    def reset(self, seed: Optional[int] = None, options: Optional[dict] = None):
        super().reset(seed=seed)
        self.current_step = self.window_size
        self.balance = self.initial_balance
        self.position = 0  # Number of shares held
        self.total_profit = 0

        return self._get_observation(), {}

    def step(self, action: int):
        # Get current price
        current_price = self.df.iloc[self.current_step]['Close']

        # Execute action
        if action == 1:  # Buy
            if self.balance >= current_price:
                shares = self.balance // current_price
                cost = shares * current_price * (1 + self.commission)
                if cost <= self.balance:
                    self.balance -= cost
                    self.position += shares

        elif action == 2:  # Sell
            if self.position > 0:
                revenue = self.position * current_price * (1 - self.commission)
                self.balance += revenue
                self.position = 0

        # Move to next step
        self.current_step += 1

        # Check if done
        done = self.current_step >= len(self.df) - 1

        # Calculate reward
        portfolio_value = self.balance + self.position * current_price
        reward = (portfolio_value - self.initial_balance) / self.initial_balance

        # Penalty for holding
        if self.position > 0:
            reward -= 0.0001

        obs = self._get_observation()
        info = {}

        return obs, reward, done, False, info

    def _get_observation(self):
        """Get current observation."""
        # Price window
        window = self.df.iloc[
            self.current_step - self.window_size:self.current_step
        ].values.flatten()

        # Current state
        current_price = self.df.iloc[self.current_step]['Close']
        state = np.array([
            self.balance / self.initial_balance,
            self.position * current_price / self.initial_balance
        ], dtype=np.float32)

        # Combine
        obs = np.concatenate([state, window]).astype(np.float32)

        return obs


class TensorboardCallback(BaseCallback):
    """Custom callback for logging training metrics."""

    def __init__(self, verbose=0):
        super().__init__(verbose)
        self.episode_rewards = []

    def _on_step(self):
        if len(self.model.ep_info_buffer) > 0:
            for info in self.model.ep_info_buffer:
                if 'r' in info:
                    self.episode_rewards.append(info['r'])

        return True


def train_rl_agent(
    df: pd.DataFrame,
    algo: str = "ppo",
    total_timesteps: int = 50000,
    output_dir: str = "./models/rl"
):
    """
    Train a reinforcement learning agent for trading.

    Args:
        df: DataFrame with OHLCV and features
        algo: RL algorithm (ppo, sac, a2c)
        total_timesteps: Total training steps
        output_dir: Output directory for model
    """
    os.makedirs(output_dir, exist_ok=True)

    # Create environment
    env = TradingEnv(df)

    # Wrap in Monitor for logging
    env = Monitor(env, output_dir)

    # Select algorithm
    if algo.lower() == "ppo":
        model = PPO("MlpPolicy", env, verbose=1, tensorboard_log=output_dir)
    elif algo.lower() == "sac":
        model = SAC("MlpPolicy", env, verbose=1, tensorboard_log=output_dir)
    elif algo.lower() == "a2c":
        model = A2C("MlpPolicy", env, verbose=1, tensorboard_log=output_dir)
    else:
        raise ValueError(f"Unknown algorithm: {algo}")

    # Train
    print(f"Training {algo.upper()} agent for {total_timesteps} steps...")
    model.learn(
        total_timesteps=total_timesteps,
        callback=TensorboardCallback(),
        progress_bar=True
    )

    # Save model
    model_path = os.path.join(output_dir, f"{algo}_trading_model")
    model.save(model_path)

    print(f"Model saved to: {model_path}")

    return model


def evaluate_agent(model, df: pd.DataFrame, episodes: int = 10):
    """Evaluate trained agent."""
    env = TradingEnv(df)

    episode_rewards = []
    episode_lengths = []

    for _ in range(episodes):
        obs, _ = env.reset()
        total_reward = 0
        done = False
        length = 0

        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, done, _, _ = env.step(action)
            total_reward += reward
            length += 1

        episode_rewards.append(total_reward)
        episode_lengths.append(length)

    print(f"Evaluation over {episodes} episodes:")
    print(f"  Mean reward: {np.mean(episode_rewards):.4f}")
    print(f"  Std reward:  {np.std(episode_rewards):.4f}")
    print(f"  Mean length: {np.mean(episode_lengths):.1f}")

    return episode_rewards


def main():
    parser = argparse.ArgumentParser(description="Train RL trading agent")
    parser.add_argument("--ticker", type=str, default="AAPL")
    parser.add_argument("--start", type=str, default="2020-01-01")
    parser.add_argument("--end", type=str, default="2024-01-01")
    parser.add_argument("--algo", type=str, default="ppo", choices=["ppo", "sac", "a2c"])
    parser.add_argument("--timesteps", type=int, default=50000)
    parser.add_argument("--output", type=str, default="./models/rl")

    args = parser.parse_args()

    # Fetch data
    print(f"Fetching data for {args.ticker}...")
    from src.data import fetch_yahoo_data
    data = fetch_yahoo_data(args.ticker, args.start, args.end)

    # Add features
    from src.features import FeatureEngine
    engine = FeatureEngine()
    features = engine.add_all_indicators(data)

    # Drop NaN
    features = features.dropna()

    print(f"Training data: {len(features)} samples")

    # Train
    model = train_rl_agent(
        features,
        algo=args.algo,
        total_timesteps=args.timesteps,
        output_dir=args.output
    )

    # Evaluate
    print("\nEvaluating agent...")
    evaluate_agent(model, features)

    print("\nNote: RL models can be converted to ONNX in limited cases.")
    print("For C++ integration, consider using the trained strategy logic.")


if __name__ == "__main__":
    main()

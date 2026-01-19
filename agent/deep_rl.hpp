#pragma once

#include "orderbook.hpp"
#include "rl_agent.hpp"
#include <vector>
#include <random>
#include <cmath>

namespace orderbook {

// Deep Q-Network (DQN) framework for RL trading
// This provides the structure for training deep RL agents

// State representation for neural network input
struct NeuralNetworkState {
    // Normalized features (all in [0, 1] or [-1, 1] range)
    std::vector<double> features;
    
    static NeuralNetworkState from_observation(const RLAgent::Observation& obs) {
        NeuralNetworkState state;
        
        const auto& market = obs.market_state;
        
        // Normalize prices (assuming prices around $100)
        double price_norm_factor = 10000.0; // $100 in ticks
        
        // Basic market features (9 features)
        state.features.push_back(market.best_bid / price_norm_factor);
        state.features.push_back(market.best_ask / price_norm_factor);
        state.features.push_back(market.spread / price_norm_factor);
        state.features.push_back(market.mid_price / price_norm_factor);
        state.features.push_back(market.order_flow_imbalance); // Already [-1, 1]
        state.features.push_back(std::tanh(market.bid_quantity / 10000.0));
        state.features.push_back(std::tanh(market.ask_quantity / 10000.0));
        state.features.push_back(market.vwap / price_norm_factor);
        state.features.push_back(std::tanh(market.price_volatility / 100.0));
        
        // Order book depth (10 bid levels + 10 ask levels = 20 features)
        for (size_t i = 0; i < 10; ++i) {
            if (i < market.bid_levels.size()) {
                state.features.push_back(market.bid_levels[i].first / price_norm_factor);
                state.features.push_back(std::tanh(market.bid_levels[i].second / 10000.0));
            } else {
                state.features.push_back(0.0);
                state.features.push_back(0.0);
            }
        }
        
        for (size_t i = 0; i < 10; ++i) {
            if (i < market.ask_levels.size()) {
                state.features.push_back(market.ask_levels[i].first / price_norm_factor);
                state.features.push_back(std::tanh(market.ask_levels[i].second / 10000.0));
            } else {
                state.features.push_back(0.0);
                state.features.push_back(0.0);
            }
        }
        
        // Position features (5 features)
        state.features.push_back(std::tanh(obs.position.quantity / 10000.0));
        state.features.push_back(std::tanh(obs.position.unrealized_pnl / 10000.0));
        state.features.push_back(std::tanh(obs.position.realized_pnl / 10000.0));
        state.features.push_back(std::tanh(obs.active_orders.size() / 10.0));
        state.features.push_back(std::tanh((obs.portfolio_value - 1000000.0) / 100000.0));
        
        return state;
    }
    
    size_t size() const { return features.size(); }
};

// Experience replay buffer for DQN
struct Experience {
    NeuralNetworkState state;
    int action;
    double reward;
    NeuralNetworkState next_state;
    bool done;
    
    Experience(const NeuralNetworkState& s, int a, double r, 
               const NeuralNetworkState& ns, bool d)
        : state(s), action(a), reward(r), next_state(ns), done(d) {}
};

class ReplayBuffer {
private:
    std::vector<Experience> buffer_;
    size_t capacity_;
    size_t index_;
    std::mt19937 rng_;
    
public:
    explicit ReplayBuffer(size_t capacity = 100000)
        : capacity_(capacity), index_(0), rng_(std::random_device{}()) {
        buffer_.reserve(capacity);
    }
    
    void add(const Experience& exp) {
        if (buffer_.size() < capacity_) {
            buffer_.push_back(exp);
        } else {
            buffer_[index_] = exp;
        }
        index_ = (index_ + 1) % capacity_;
    }
    
    std::vector<Experience> sample(size_t batch_size) {
        std::vector<Experience> batch;
        batch.reserve(batch_size);
        
        std::uniform_int_distribution<size_t> dist(0, buffer_.size() - 1);
        
        for (size_t i = 0; i < batch_size && i < buffer_.size(); ++i) {
            batch.push_back(buffer_[dist(rng_)]);
        }
        
        return batch;
    }
    
    size_t size() const { return buffer_.size(); }
    bool is_ready(size_t min_size) const { return buffer_.size() >= min_size; }
};

// Epsilon-greedy exploration strategy
class EpsilonGreedy {
private:
    double epsilon_;
    double epsilon_min_;
    double epsilon_decay_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    
public:
    EpsilonGreedy(double epsilon = 1.0, double epsilon_min = 0.01, 
                  double epsilon_decay = 0.995)
        : epsilon_(epsilon), epsilon_min_(epsilon_min), 
          epsilon_decay_(epsilon_decay), rng_(std::random_device{}()),
          dist_(0.0, 1.0) {}
    
    bool should_explore() {
        return dist_(rng_) < epsilon_;
    }
    
    void decay() {
        epsilon_ = std::max(epsilon_min_, epsilon_ * epsilon_decay_);
    }
    
    double get_epsilon() const { return epsilon_; }
    
    int random_action(int num_actions) {
        std::uniform_int_distribution<int> action_dist(0, num_actions - 1);
        return action_dist(rng_);
    }
};

// Q-Learning update interface
// In production, this would connect to a neural network library (PyTorch, TensorFlow)
class QLearningAgent {
private:
    // Simplified Q-table for demonstration (state features -> action values)
    // In practice, use a neural network
    struct QTable {
        std::unordered_map<int, std::vector<double>> table;
        int num_actions;
        double learning_rate;
        double discount_factor;
        
        QTable(int n_actions, double lr = 0.001, double gamma = 0.99)
            : num_actions(n_actions), learning_rate(lr), discount_factor(gamma) {}
        
        std::vector<double> get_q_values(int state_hash) {
            if (table.find(state_hash) == table.end()) {
                table[state_hash] = std::vector<double>(num_actions, 0.0);
            }
            return table[state_hash];
        }
        
        void update(int state_hash, int action, double target) {
            auto& q_values = table[state_hash];
            q_values[action] += learning_rate * (target - q_values[action]);
        }
        
        int get_best_action(int state_hash) {
            auto q_values = get_q_values(state_hash);
            return std::distance(q_values.begin(), 
                               std::max_element(q_values.begin(), q_values.end()));
        }
    };
    
    QTable q_table_;
    EpsilonGreedy exploration_;
    ReplayBuffer replay_buffer_;
    
    // Simple hash function for state (in practice, use neural network)
    int hash_state(const NeuralNetworkState& state) {
        int hash = 0;
        for (size_t i = 0; i < std::min(state.features.size(), size_t(10)); ++i) {
            hash ^= std::hash<double>{}(state.features[i]) << i;
        }
        return hash;
    }
    
public:
    QLearningAgent(int num_actions = 8)
        : q_table_(num_actions), replay_buffer_(100000) {}
    
    int select_action(const NeuralNetworkState& state) {
        if (exploration_.should_explore()) {
            return exploration_.random_action(q_table_.num_actions);
        } else {
            return q_table_.get_best_action(hash_state(state));
        }
    }
    
    void train_step(const Experience& exp) {
        replay_buffer_.add(exp);
        
        if (!replay_buffer_.is_ready(32)) {
            return;
        }
        
        // Sample batch and update Q-values
        auto batch = replay_buffer_.sample(32);
        
        for (const auto& experience : batch) {
            int state_hash = hash_state(experience.state);
            int next_state_hash = hash_state(experience.next_state);
            
            auto next_q_values = q_table_.get_q_values(next_state_hash);
            double max_next_q = *std::max_element(next_q_values.begin(), 
                                                  next_q_values.end());
            
            double target = experience.reward;
            if (!experience.done) {
                target += q_table_.discount_factor * max_next_q;
            }
            
            q_table_.update(state_hash, experience.action, target);
        }
        
        exploration_.decay();
    }
    
    double get_exploration_rate() const { return exploration_.get_epsilon(); }
};

// Training loop helper
class TrainingEngine {
private:
    OrderBook& orderbook_;
    RLAgent& agent_;
    MarketSimulator& simulator_;
    QLearningAgent q_agent_;
    
    size_t episode_;
    size_t total_steps_;
    std::vector<double> episode_rewards_;
    
public:
    TrainingEngine(OrderBook& book, RLAgent& agent, MarketSimulator& sim)
        : orderbook_(book), agent_(agent), simulator_(sim),
          episode_(0), total_steps_(0) {}
    
    void train_episode(size_t max_steps = 1000) {
        agent_.reset();
        double episode_reward = 0.0;
        
        for (size_t step = 0; step < max_steps; ++step) {
            // Simulate market
            simulator_.simulate_step(5);
            
            // Get state
            auto obs = agent_.get_observation();
            auto state = NeuralNetworkState::from_observation(obs);
            
            // Select action
            int action_idx = q_agent_.select_action(state);
            auto action = static_cast<RLAgent::Action>(action_idx);
            
            // Execute action
            auto reward = agent_.execute_action(action, 500);
            
            // Get next state
            auto next_obs = agent_.get_observation();
            auto next_state = NeuralNetworkState::from_observation(next_obs);
            
            // Store experience
            bool done = (step == max_steps - 1);
            Experience exp(state, action_idx, reward.total, next_state, done);
            q_agent_.train_step(exp);
            
            episode_reward += reward.total;
            ++total_steps_;
        }
        
        episode_rewards_.push_back(episode_reward);
        ++episode_;
    }
    
    void train(size_t num_episodes, size_t steps_per_episode = 1000) {
        std::cout << "Starting training for " << num_episodes << " episodes..." << std::endl;
        
        for (size_t ep = 0; ep < num_episodes; ++ep) {
            train_episode(steps_per_episode);
            
            if (ep % 10 == 0) {
                double avg_reward = 0.0;
                size_t start = ep >= 10 ? ep - 10 : 0;
                for (size_t i = start; i <= ep; ++i) {
                    avg_reward += episode_rewards_[i];
                }
                avg_reward /= (ep - start + 1);
                
                std::cout << "Episode " << ep 
                          << " | Avg Reward: " << avg_reward
                          << " | Epsilon: " << q_agent_.get_exploration_rate()
                          << " | Total Steps: " << total_steps_ << std::endl;
            }
        }
        
        std::cout << "Training complete!" << std::endl;
    }
    
    const std::vector<double>& get_episode_rewards() const {
        return episode_rewards_;
    }
};

} // namespace orderbook

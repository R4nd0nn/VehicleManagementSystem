#ifndef DATABASE_H
#define DATABASE_H

#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <pqxx/pqxx>

class ConnectionPool {
public:
    ConnectionPool(const std::string& conn_str, int pool_size = 10)
        : conn_str_(conn_str), pool_size_(pool_size), stop_(false) {
        for (int i = 0; i < pool_size; ++i) {
            try {
                auto conn = std::make_unique<pqxx::connection>(conn_str_);
                if (conn->is_open()) {
                    pool_.push(std::move(conn));
                    available_count_++;
                } else {
                    throw std::runtime_error("Failed to open connection");
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to create connection " << i << ": " << e.what() << std::endl;
            }
        }
        std::cout << "连接池初始化完成，可用连接数: " << available_count_ << std::endl;
    }
    
    ~ConnectionPool() {
        stop_ = true;
        cv_.notify_all();
    }
    
    // 获取连接（阻塞直到有可用连接）
    std::unique_ptr<pqxx::connection> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { 
            return stop_ || !pool_.empty(); 
        });
        
        if (stop_ || pool_.empty()) {
            return nullptr;
        }
        
        auto conn = std::move(pool_.front());
        pool_.pop();
        return conn;
    }
    
    // 归还连接
    void returnConnection(std::unique_ptr<pqxx::connection> conn) {
        if (!conn || !conn->is_open()) {
            // 连接已失效，创建新连接补充
            try {
                conn = std::make_unique<pqxx::connection>(conn_str_);
            } catch (const std::exception& e) {
                std::cerr << "Failed to recreate connection: " << e.what() << std::endl;
                return;
            }
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(std::move(conn));
        cv_.notify_one();
    }
    
    // RAII 连接包装器，自动归还
    class ConnectionGuard {
    public:
        ConnectionGuard(ConnectionPool& pool) : pool_(pool), conn_(pool_.getConnection()) {}
        
        ~ConnectionGuard() {
            if (conn_) {
                pool_.returnConnection(std::move(conn_));
            }
        }
        
        // 禁止拷贝
        ConnectionGuard(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(const ConnectionGuard&) = delete;
        
        // 移动构造
        ConnectionGuard(ConnectionGuard&& other) noexcept 
            : pool_(other.pool_), conn_(std::move(other.conn_)) {}
        
        // 判断是否有效
        bool isValid() const { return conn_ != nullptr && conn_->is_open(); }
        
        // 获取连接引用 - 关键！
        pqxx::connection& operator*() { return *conn_; }
        
        // 获取连接指针
        pqxx::connection* operator->() { return conn_.get(); }
        
    private:
        ConnectionPool& pool_;
        std::unique_ptr<pqxx::connection> conn_;
    };
    
    int getAvailableCount() const { return available_count_; }
    
private:
    std::string conn_str_;
    int pool_size_;
    int available_count_;
    std::atomic<bool> stop_;
    std::queue<std::unique_ptr<pqxx::connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

#endif
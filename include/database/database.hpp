#pragma once

#include <mysql/mysql.h>
#include <stdexcept>
#include <algorithm>

extern MYSQL *db;

extern void mysql_connect();

class hStmt
{
public:
    hStmt(const std::string &query);
   ~hStmt();

    hStmt           (const hStmt &) = delete;
    hStmt &operator=(const hStmt &) = delete;

    void bind_param(MYSQL_BIND *param);
    void execute();
    bool fetch();

    /* https://dev.mysql.com/doc/mysql-errors/8.4/en/client-error-reference.html */
    void log_err() { std::fprintf(stderr, "[MariaDB] %s\n", mysql_error(db)); }

    MYSQL_STMT *pStmt;
};

class hTransaction
{
public:
    hTransaction() { if (mysql_query(db, "START TRANSACTION")) throw std::runtime_error("Unable to begin save"); }
    ~hTransaction() { if (!committed) mysql_rollback(db); }
    hTransaction(const hTransaction &) = delete;
    hTransaction &operator=(const hTransaction &) = delete;
    void commit()
    {
        if (mysql_commit(db)) throw std::runtime_error("Unable to finish save");
        committed = true;
    }
private:
    bool committed{};
};

struct blob
{
    /* @note template vibes */
    void f32(float val) {
        int size = mData.size();
        mData.resize(size + sizeof(float));
        memcpy(mData.data() + size, &val, sizeof(float));
    }
    void i32(int val)   { 
        int size = mData.size();
        mData.resize(size + sizeof(int));
        memcpy(mData.data() + size, &val, sizeof(int));
    }
    void u32(u_int val)   { 
        int size = mData.size();
        mData.resize(size + sizeof(u_int));
        memcpy(mData.data() + size, &val, sizeof(u_int));
    }
    void i16(short val) {
        int size = mData.size();
        mData.resize(size + sizeof(short));
        memcpy(mData.data() + size, &val, sizeof(short));
    }
    void read_string(std::string &value, int &pos) const
    {
        short length{};
        read_value(length, pos);
        if (length < 0) throw std::out_of_range("Invalid saved string length");
        require(pos, static_cast<std::size_t>(length));
        value.assign(mData.begin() + pos, mData.begin() + pos + length);
        pos += length;
    }
    void read_u32(u_int &value, int &pos) const { read_value(value, pos); }
    void read_i32(int &value, int &pos) const { read_value(value, pos); }
    void read_i16(short &value, int &pos) const { read_value(value, pos); }
    void read_u8(u_char &value, int &pos) const { read_value(value, pos); }
    void read_f32(float &value, int &pos) const { read_value(value, pos); }
    void u8(u_char val) {
        mData.push_back(val);
    }
    void push_back(const blob &blob) 
    { 
        const auto &data = blob.data();
        
        mData.insert(mData.end(), data.cbegin(), data.cend());
    }
    const std::vector<u_char> &data() const noexcept { 
        return mData; 
    }
    std::vector<u_char> &data() noexcept {
        return mData;
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { 
        return mData.size(); 
    }
    constexpr void resize(std::size_t __new_size)
    {
        mData.resize(__new_size);
    }

private:
    void require(int pos, std::size_t length) const
    {
        if (pos < 0 || static_cast<std::size_t>(pos) > mData.size()) throw std::out_of_range("Invalid save offset");
        if (length > mData.size() - pos) throw std::out_of_range("Truncated saved data");
    }
    template<typename T>
    void read_value(T &value, int &pos) const
    {
        require(pos, sizeof(T));
        std::copy_n(mData.data() + pos, sizeof(T), reinterpret_cast<u_char*>(&value));
        pos += sizeof(T);
    }
    std::vector<u_char> mData;
};

extern MYSQL_BIND make_bind_in(const signed &buffer);
extern MYSQL_BIND make_bind_in(const unsigned &buffer);
extern MYSQL_BIND make_bind_in(const long &buffer);
extern MYSQL_BIND make_bind_in(const long long &buffer);
extern MYSQL_BIND make_bind_in(const float &buffer);
extern MYSQL_BIND make_bind_in(const std::string &buffer);
extern MYSQL_BIND make_bind_in(const std::vector<u_char> &buffer);
extern MYSQL_BIND make_bind_in(const ::blob &buffer);

extern MYSQL_BIND make_bind_out(signed &buffer);
extern MYSQL_BIND make_bind_out(unsigned &buffer);
extern MYSQL_BIND make_bind_out(long &buffer);
extern MYSQL_BIND make_bind_out(long long &buffer);
extern MYSQL_BIND make_bind_out(float &buffer);
extern MYSQL_BIND make_bind_out(std::string &buffer);
extern MYSQL_BIND make_bind_out(std::vector<u_char> &buffer);
extern MYSQL_BIND make_bind_out(::blob &buffer);

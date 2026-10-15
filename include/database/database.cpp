#include "pch.hpp"

#include "database.hpp"
#include "database_config.hpp"

MYSQL *db;

void create_table_if_not_exist()
{
    std::string query_peer = 
        "CREATE TABLE IF NOT EXISTS peer ("
            "uid INT AUTO_INCREMENT PRIMARY KEY,"
            "growid VARCHAR(18) UNIQUE,"
            "password VARCHAR(18),"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "inventory BLOB,"
            "state BLOB"
        ")";
    std::string query_world = 
        "CREATE TABLE IF NOT EXISTS world ("
            "name VARCHAR(24) NOT NULL PRIMARY KEY,"
            "owner INT DEFAULT 0,"
            "blocks BLOB,"
            "objects BLOB,"
            "state BLOB"
        ")";
    if (mysql_query(db, query_peer.c_str()) || mysql_query(db, query_world.c_str()))
    {
        std::fprintf(stderr, "%s\n", mysql_error(db));
    }
    if (mysql_query(db, "ALTER TABLE peer ADD COLUMN IF NOT EXISTS state BLOB") ||
        mysql_query(db, "ALTER TABLE world ADD COLUMN IF NOT EXISTS state BLOB"))
        std::fprintf(stderr, "[MariaDB] %s\n", mysql_error(db));

}

void mysql_connect()
{
    db = mysql_init(NULL);

    if (mysql_real_connect(db, gDb_config.host.c_str(), gDb_config.user.c_str(), gDb_config.passwd.c_str(), NULL, 3306u, NULL, 0ul) == NULL) 
    {
        std::fprintf(stderr, "[MariaDB] %s\n", mysql_error(db));
    }
    else printf("connected to MariaDB server on %s:%d\n", db->host, db->port);

    mysql_query(db, "CREATE DATABASE IF NOT EXISTS gurotopia");
    mysql_select_db(db, "gurotopia");

    create_table_if_not_exist();
}

/* hStmt */

hStmt::hStmt(const std::string &query)
{
    this->pStmt = mysql_stmt_init(db);
    if (!pStmt) throw std::runtime_error("Unable to allocate database statement");
    if (mysql_stmt_prepare(pStmt, query.c_str(), static_cast<u_long>(query.size())))
    {
        mysql_stmt_close(pStmt);
        throw std::runtime_error("Unable to prepare database statement");
    }
}
hStmt::~hStmt() 
{
    if (mysql_stmt_close(pStmt))
    {
        std::fprintf(stderr, "%s\n", mysql_error(db));
    }
}

void hStmt::bind_param(MYSQL_BIND *param)
{
    if (mysql_stmt_bind_param(pStmt, param)) log_err();
}
void hStmt::execute()
{
    if (mysql_stmt_execute(pStmt)) throw std::runtime_error("Database statement failed");
}
bool hStmt::fetch()
{
    const int result = mysql_stmt_fetch(pStmt);
    if (result == 0) return true;
    if (result == MYSQL_NO_DATA) return false;
    throw std::runtime_error("Failed to read saved data");
}

/* ~ hStmt ~ */


MYSQL_BIND make_bind_in(const signed &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const unsigned &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_in(const long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const long long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_in(const float &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_in(const std::string &buffer)
{
    return { .buffer = (void*)buffer.c_str(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_in(const std::vector<u_char> &buffer)
{
    return { .buffer = (void*)buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_in(const ::blob &buffer)
{
    return { .buffer = (void*)buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}

MYSQL_BIND make_bind_out(signed &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(unsigned &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_out(long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(long long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_out(float &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_out(std::string &buffer)
{
    buffer.resize(1024, '\0');

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_out(std::vector<u_char> &buffer)
{
    buffer.resize(65535);

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_out(::blob &buffer)
{
    buffer.resize(65535);

    return { .buffer = buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}

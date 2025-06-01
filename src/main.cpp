#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>
#include <mutex>

using boost::asio::ip::tcp;
std::mutex file_mutex;

#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32];
    std::time_t timestamp;
    double value;
};
#pragma pack(pop)

std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

void save_log_record(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(file_mutex);
    std::ofstream file("sensor_" + std::string(record.sensor_id) + ".bin", std::ios::binary | std::ios::app);
    file.write(reinterpret_cast<const char*>(&record), sizeof(record));
}

std::vector<LogRecord> get_last_records(const std::string& sensor_id, int n) {
    std::vector<LogRecord> result;
    std::ifstream file("sensor_" + sensor_id + ".bin", std::ios::binary);
    if (!file.is_open()) return result;

    file.seekg(0, std::ios::end);
    std::streamsize filesize = file.tellg();
    std::streamsize record_size = sizeof(LogRecord);
    int total_records = filesize / record_size;
    int records_to_read = std::min(n, total_records);

    file.seekg((total_records - records_to_read) * record_size, std::ios::beg);
    for (int i = 0; i < records_to_read; ++i) {
        LogRecord rec;
        file.read(reinterpret_cast<char*>(&rec), record_size);
        result.push_back(rec);
    }
    return result;
}

class session : public std::enable_shared_from_this<session> {
public:
    session(tcp::socket socket) : socket_(std::move(socket)), sensor_announced_(false) {}

    void start() {
        read_command();
    }

private:
    void read_command() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    std::istream is(&buffer_);
                    std::string command;
                    std::getline(is, command);
                    if (!command.empty() && command.back() == '\r') {
                        command.pop_back();
                    }
                    process_command(command);
                    read_command();
                }
            });
    }

    void process_command(const std::string& command) {
        if (command.find("LOG|") == 0) {
            process_log(command);
        } else if (command.find("GET|") == 0) {
            process_get(command);
        }
    }

    void process_log(const std::string& cmd) {
        std::istringstream ss(cmd);
        std::string part;

        std::getline(ss, part, '|'); // LOG
        std::getline(ss, part, '|');
        std::string sensor_id = part;
        std::getline(ss, part, '|');
        std::time_t timestamp = string_to_time_t(part);
        std::getline(ss, part, '|');
        double value = std::stod(part);

        if (!sensor_announced_) {
            std::cout << "Sensor conectado: " << sensor_id << std::endl;
            sensor_announced_ = true;
        }

        LogRecord record{};
        std::snprintf(record.sensor_id, sizeof(record.sensor_id), "%s", sensor_id.c_str());
        record.timestamp = timestamp;
        record.value = value;

        save_log_record(record);
    }

    void process_get(const std::string& cmd) {
        std::istringstream ss(cmd);
        std::string part;

        std::getline(ss, part, '|'); // GET
        std::getline(ss, part, '|');
        std::string sensor_id = part;
        std::getline(ss, part, '|');
        int n = std::stoi(part);

        auto records = get_last_records(sensor_id, n);
        if (records.empty()) {
            send_message("ERROR|INVALID_SENSOR_ID\r\n");
            return;
        }

        std::ostringstream response;
        response << records.size();
        for (const auto& r : records) {
            response << ";" << time_t_to_string(r.timestamp) << "|" << r.value;
        }
        response << "\r\n";
        send_message(response.str());
    }

    void send_message(const std::string& msg) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(msg),
            [this, self](boost::system::error_code, std::size_t) {});
    }

    tcp::socket socket_;
    boost::asio::streambuf buffer_;
    bool sensor_announced_;
};

class server {
public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        accept();
    }

private:
    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<session>(std::move(socket))->start();
                }
                accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main() {
    try {
        boost::asio::io_context io_context;
        server s(io_context, 9000);
        std::cout << "Servidor rodando na porta 9000..." << std::endl;
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Erro: " << e.what() << std::endl;
    }
    return 0;
}

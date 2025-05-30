// ======= INCLUDES =======
#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <ctime>

using boost::asio::ip::tcp;

// ======= DEFINIÇÕES AUXILIARES =======
struct LogRecord {
    int sensor_id;
    std::time_t timestamp;
    std::string data;
};

void save_log_record(const LogRecord& record) {
    std::ofstream file("sensor_" + std::to_string(record.sensor_id) + ".bin", std::ios::binary | std::ios::app);
    file.write(reinterpret_cast<const char*>(&record.timestamp), sizeof(record.timestamp));
    file.write(record.data.c_str(), record.data.size());
    file.close();
}

// ======= CLASSE SESSION =======
class session : public std::enable_shared_from_this<session> {
public:
    session(tcp::socket socket) : socket_(std::move(socket)) { }

    void start() {
        read_command();
    }

private:
    void read_command() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    std::istream is(&buffer_);
                    std::string command;
                    std::getline(is, command);
                    process_command(command);
                }
            });
    }

    void process_command(const std::string& command) {
        if (command.find("LOG") == 0) {
            process_log(command);
        } else if (command.find("DATA") == 0) {
            process_data(command);
        }
        // Continua escutando comandos
        read_command();
    }

    void process_log(const std::string& cmd) {
        LogRecord rec;
        rec.sensor_id = 1; // Exemplo, pode ser extraído do comando
        rec.timestamp = std::time(nullptr);
        rec.data = cmd + "\n";
        save_log_record(rec);
        std::cout << "LOG salvo.\n";
    }

    void process_data(const std::string& cmd) {
        std::cout << "DATA recebido: " << cmd << std::endl;
    }

    tcp::socket socket_;
    boost::asio::streambuf buffer_;
};

// ======= CLASSE SERVER =======
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
                accept();  // Continua aceitando novas conexões
            });
    }

    tcp::acceptor acceptor_;
};

// ======= FUNÇÃO PRINCIPAL =======
int main() {
    try {
        boost::asio::io_context io_context;
        server s(io_context, 12345);  // Porta fixa ou configurável
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Erro: " << e.what() << std::endl;
    }
    return 0;
}

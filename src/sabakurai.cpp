#include <cstdint>
#include <ncurses.h>
#include <chrono>
#include <thread>
#include <string>
#include <iostream>
#include <fstream>

#include "socket.hpp"

#include "box.hpp"
#include "split.hpp"
#include "title.hpp"
#include "player_list.hpp"
#include "log.hpp"
#include "command_prompt.hpp"
#include "playing.hpp"

std::ofstream file("debug.log");

auto get_ip(const std::string & ip_string) -> in_addr_t;

using LogCallback = std::function<void(uint32_t color, const std::string & message)>;

struct Command {
	std::string name;
	std::function<void(const std::vector<std::string> & args)> callback;

	auto matches(const std::string & raw, uint32_t end) const -> bool {
		if (end != name.size() + 1) {
			return false;
		}
		auto i = 1;
		for (; i < end; ++i) {
			if (raw[i] != name[i - 1]) {
				return false;
			}
		}
		if (i != end) {
			return false;
		} else {
			return true;
		}
	}
};

auto dispatch_command(const std::string & raw, const std::vector<Command> & commands, Log & log) -> void;
auto get_args(int32_t start, const std::string & raw) -> std::vector<std::string>;

auto get_color(std::string color) -> int8_t;

struct LobbyState {
	Box root;
	Split split;
	Box title_box;
	Title title;
	Box player_box;
	PlayerList players;
	Box log_box;
	Log log;
	Box command_box;
	CommandPrompt command;
	Split split2;
	Split split3;

	Socket socket;
	uint32_t curr_addr;
};

auto disconnected(LobbyState & lobby)                  -> uint32_t;
auto connect(LobbyState & lobby)                       -> void;
auto connected(LobbyState & lobby)                     -> uint32_t;
auto playing(LobbyState & lobby, bool newly_connected) -> uint32_t;

const static auto
	DISCONNECTED = 0x00,
	CONNECTED    = 0x01,
	RECONNECT    = 0x02,
	QUIT         = 0x03,
	PLAYING      = 0x04,
	LOBBY        = 0x05,
	MID_PLAYING  = 0x06;

const static auto
	COLOR_DEFAULT  = 0x00,
	COLOR_INVERTED = 0x01,
	COLOR_ERROR    = 0x02;

static auto
	COLOR_BLUE_B    = 0x03,
	COLOR_GREEN_B   = 0x04,
	COLOR_CYAN_B    = 0x05,
	COLOR_RED_B     = 0x06,
	COLOR_MAGENTA_B = 0x07,
	COLOR_YELLOW_B  = 0x08,
	COLOR_WHITE_B   = 0x09;

auto main(int32_t argc, char ** argv) -> int32_t {
	LobbyState lobby = {
		{ "" },

		{
			lobby.root,
			[](uint32_t x, uint32_t y, uint32_t width, uint32_t height) -> uint32_t {
				return height < 7 ? height : 7;
			},
			Split::HORZ
		},

		{ lobby.root, "" },
		{ lobby.root, true },

		{ lobby.root, "Players" },
		{ lobby.root, true },

		{ lobby.root, "Log" },
		{ lobby.root, true },

		{ lobby.root, "Command" },
		{ lobby.root, true },
		{
			lobby.root,
			[](uint32_t x, uint32_t y, uint32_t width, uint32_t height) -> uint32_t {
				return width * 0.8;
			},
			Split::VERT
		},
		{
			lobby.root,
			[&lobby](uint32_t x, uint32_t y, uint32_t width, uint32_t height) -> uint32_t {
				if (lobby.command.line_count() + 2 > height - (height * (3.0 / 4.0))) {
					return height * (3.0 / 4.0);
				} else {
					return height - lobby.command.line_count() - 2;
				}
			},
			Split::HORZ
		},
		{ PF_INET, SOCK_STREAM, IPPROTO_TCP }
	};

	cbreak();
	noecho();

	if (!has_colors()) {
		return -1;
	}

	start_color();

	if (COLOR_PAIRS < 3) {
		return -2;
	}
	
	init_pair(COLOR_DEFAULT,  COLOR_WHITE, COLOR_BLACK);
	init_pair(COLOR_INVERTED, COLOR_BLACK, COLOR_WHITE);
	init_pair(COLOR_ERROR,    COLOR_BLACK, COLOR_RED  );

	if (COLOR_PAIRS < 9) {
		COLOR_BLUE_B    =
		COLOR_GREEN_B   =
		COLOR_CYAN_B    =
		COLOR_RED_B     =
		COLOR_MAGENTA_B =
		COLOR_YELLOW_B  =
		COLOR_WHITE_B   = 0x00;
		lobby.log.message(COLOR_RED_B, "Unfortunately your terminal does not support enough colors for custom player colors. You will still be able to set your own but you will not be able to see any special colors.");
	} else {
		init_pair(COLOR_BLUE_B,    COLOR_BLUE,    COLOR_BLACK);
		init_pair(COLOR_GREEN_B,   COLOR_GREEN,   COLOR_BLACK);
		init_pair(COLOR_CYAN_B,    COLOR_CYAN,    COLOR_BLACK);
		init_pair(COLOR_RED_B,     COLOR_RED,     COLOR_BLACK);
		init_pair(COLOR_MAGENTA_B, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(COLOR_YELLOW_B,  COLOR_YELLOW,  COLOR_BLACK);
		init_pair(COLOR_WHITE_B,   COLOR_WHITE,   COLOR_BLACK);
	}

	lobby.title_box.set_child(&lobby.title);
	lobby.player_box.set_child(&lobby.players);
	lobby.log_box.set_child(&lobby.log);
	lobby.command_box.set_child(&lobby.command);
	lobby.split3.set_children(&lobby.log_box, &lobby.command_box);
	lobby.split2.set_children(&lobby.split3, &lobby.player_box);
	lobby.split.set_children(&lobby.title_box, &lobby.split2);
	lobby.root.set_child(&lobby.split);

	lobby.log.message("Welcome to sabakurai! For assistance please enter /help");

	lobby.root.draw().refresh();

	auto state = DISCONNECTED;
	while (state != QUIT) {
		switch (state) {
			case DISCONNECTED: state = disconnected(lobby);   break;
			case CONNECTED:
			case RECONNECT:    connect(lobby);
			case LOBBY:        state = connected(lobby);      break;
			case PLAYING:      state = playing(lobby, false); break;
			case MID_PLAYING:  state = playing(lobby, true);  break;
		}
	}

	return 0;
}

auto disconnected(LobbyState & lobby) -> uint32_t {
	auto spectator = lobby.players.get_self().spectate;

	auto quit = DISCONNECTED;

	////// COMMANDS //////

	auto commands = std::vector<Command>{
		Command{
			"say",
			[&lobby](const std::vector<std::string> & args) -> void {
				auto message = std::string();
				for (auto i = 0; i < args.size(); ++i) {
					message.append(args[i]);
					if (i < args.size() - 1) {
						message.append(1, ' ');
					}
				}
				auto & self = lobby.players.get_self();
				lobby.log.message(self.color, self.name, message);
				lobby.log.draw().refresh();
			}
		},
		Command{
			"connect",
			[&quit, &lobby](const std::vector<std::string> & args) -> void {
				lobby.log.message(COLOR_DEFAULT, std::string("Attempting to connect to server '").append(args[0]).append(1, '\''));
				lobby.log.draw().refresh();
				if (lobby.socket.addressInfo({
					AF_INET,
					htons(PORT),
					lobby.curr_addr = get_ip(args[0])
				}).connect(5000)) {
					auto & player = lobby.players.get_self();

					lobby.socket
						<< uint8_t( player.color )
						<< uint8_t( player.name.size() )
						<< player.name
						<< Socket::FLUSH;

					lobby.log.message(COLOR_DEFAULT, std::string("Connected to server '").append(args[0]).append(1, '\''));
					auto state = lobby.socket.read();
					if (state == 0) {
						quit = CONNECTED;
					} else {
						quit = MID_PLAYING;
					}
				} else {
					lobby.log.message(COLOR_RED_B, std::string("Failed to connect to server '").append(args[0]).append(1, '\''));
					lobby.curr_addr = 0;
				}
				lobby.log.draw().refresh();
			}
		},
		Command{
			"name",
			[&lobby](const std::vector<std::string> & args) -> void {
				auto name = std::string();
				for (auto i = 0; i < args.size(); ++i) {
					name.append(args[i]);
					if (i < args.size() - 1) {
						name.append(1, ' ');
					}
				}
				if (name.size() > 0xFF) {
					lobby.log.message(COLOR_RED_B, "Sorry. Max name length is 255.");
					lobby.log.draw().refresh();
				} else {
					lobby.players.get_self().name = name;
					lobby.players.draw().refresh();
				}
			}
		},
		Command{
			"help",
			[&lobby](const std::vector<std::string> & args) -> void {
				lobby.log.message(
					"Type a message and hit enter to send. Shift + Tab is new line. To enter a command prefix the message with a '/'. To send a message that starts with a '/' use a '//' at the start.\n"
					"Commands:\n"
					"- /help          - prints the help menu\n"
					"- /quit          - quits the game\n"
					"- /connect <ip>  - connects to the server at the ip\n"
					"- /name <string> - changes your name\n"
					"- /color <color> - changes your player color. one of blue, green, cyan, red, magenta, yellow, or white\n"
				);
				lobby.log.draw().refresh();
			}
		},
		Command{
			"quit",
			[&quit](const std::vector<std::string> & args) -> void {
				quit = QUIT;
			}
		},
		Command{
			"killme",
			[&lobby](const std::vector<std::string> & args) -> void {
				auto & self = lobby.players.get_self();
				lobby.log.message("Opens fridge...");
				lobby.log.message(self.color, self.name, "Kill me");
				lobby.log.message(COLOR_RED_B, "Medic", "Later");
				lobby.log.draw().refresh();
			}
		},
		Command{
			"color",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (args.size() != 1) {
					lobby.log.message(COLOR_RED_B, "/color expects one argument");
					lobby.log.draw().refresh();
					return;
				}	
				auto color = get_color(args[0]);
				if (color == -1) {
					lobby.log.message(COLOR_RED_B, "/color expects one of blue, green, cyan, red, magenta, yellow, or white");
					lobby.log.draw().refresh();
					return;
				}
				lobby.players.get_self().color = color;
				lobby.players.draw().refresh();
			}
		}
	};
	
	while (quit == DISCONNECTED) {
		if (lobby.command.update()) {
			if (lobby.command.complete()) {
				dispatch_command(lobby.command.get(), commands, lobby.log);
				lobby.command.clear_command();
			}
			if (lobby.command.height_change(lobby.command.height() + 1)) {
				lobby.split3.component_resize();
				lobby.log_box.draw();
				lobby.command_box.draw();
				lobby.split3.refresh();
			} else {
				lobby.command.draw().refresh();
			}
			lobby.split3.refresh();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(15));
	}

	lobby.players.get_self().spectate = spectator;

	return quit;
}

auto connect(LobbyState & lobby) -> void {
	auto my_id = lobby.socket.read();
	auto player_count = lobby.socket.read();
	for (auto i = uint32_t{ 0 }; i < player_count; ++i) {
		auto id    = uint32_t( lobby.socket.read() );
		auto spec  = bool( lobby.socket.read() );
		auto color = lobby.socket.read();
		auto name  = std::string();
		lobby.socket.width(Socket::U8) >> name;
		auto ready = bool( lobby.socket.read() );
		if (id == my_id) {
			lobby.players.get_self().id = my_id;
		} else {
			lobby.players.add_player(id, color, ready, spec, name);
		}
	}
	lobby.players.draw().refresh();
}

auto connected(LobbyState & lobby) -> uint32_t {
	file << "Connected" << std::endl;
		
	auto quit = CONNECTED;

	////// COMMANDS //////

	auto commands = std::vector<Command>{
		Command{
			"say",
			[&lobby](const std::vector<std::string> & args) -> void {
				auto message = std::string();
				for (auto i = 0; i < args.size(); ++i) {
					message.append(args[i]);
					if (i < args.size() - 1) {
						message.append(1, ' ');
					}
				}
				lobby.socket
					<< uint8_t( 0x05 )
					<< uint16_t( message.size() )
					<< message
					<< Socket::FLUSH;
			}
		},
		Command{
			"quit",
			[&quit](const std::vector<std::string> & args) -> void {
				quit = QUIT;
			}
		},
		Command{
			"disconnect",
			[&quit, &lobby](const std::vector<std::string> & args) -> void {
				lobby.socket
					<< uint8_t( 0x07 )
					<< Socket::FLUSH;
				lobby.log.message(COLOR_DEFAULT, "Disconnected from server");
				lobby.log.draw().refresh();
				lobby.players.clear_list();
				lobby.players.draw().refresh();
				lobby.socket.close();
				quit = DISCONNECTED;
			}
		},
		Command{
			"connect",
			[&quit, &lobby](const std::vector<std::string> & args) -> void {
				auto new_ip = get_ip(args[0]);
				file << "New ip: " << new_ip << ", Old ip: " << lobby.curr_addr << std::endl;
				if (new_ip != lobby.curr_addr) {
					lobby.socket
						<< uint8_t( 0x07 )
						<< Socket::FLUSH;
					lobby.socket.close();
					lobby.log.message(COLOR_DEFAULT, std::string("Disconnected from server. Attempting to connect to server '").append(args[0]).append(1, '\''));
					lobby.log.draw().refresh();
					lobby.players.clear_list();
					lobby.players.draw().refresh();
					if (lobby.socket.addressInfo({
						AF_INET,
						htons(PORT),
						lobby.curr_addr = new_ip
					}).connect(5000)) {
						auto & player = lobby.players.get_self();

						lobby.socket
							<< uint8_t( player.color )
							<< uint8_t( player.name.size() )
							<< player.name
							<< Socket::FLUSH;

						lobby.log.message(COLOR_DEFAULT, std::string("Connected to server '").append(args[0]).append(1, '\''));

						auto state = lobby.socket.read();
						if (state == 0) {
							quit = RECONNECT;
						} else {
							quit = MID_PLAYING;
						}
					} else {
						lobby.log.message(COLOR_RED_B, std::string("Failed to connect to server '").append(args[0]).append(1, '\''));
						lobby.curr_addr = 0;
						quit = DISCONNECTED;
					}
				} else {
					lobby.log.message(COLOR_DEFAULT, std::string("Already connected to server '").append(args[0]).append(1, '\''));
				}
				lobby.log.draw().refresh();
			}
		},
		Command{
			"help",
			[&lobby](const std::vector<std::string> & args) -> void {
				lobby.log.message(
					"Type a message and hit enter to send. Shift + Tab is new line. To enter a command prefix the message with a '/'. To send a message that starts with a '/' use a '//' at the start.\n"
					"Commands:\n"
					"- /help          - prints the help menu\n"
					"- /quit          - quits the game\n"
					"- /disconnect    - disconnects from the server\n"
					"- /connect <ip>  - connects to the server at the ip\n"
					"- /ready         - set your ready status\n"
					"- /unready       - unset your ready status\n"
					"- /spectate      - set yourself to be a spectator\n"
					"- /partake       - set yourself to be a participant\n"
					"- /name <string> - changes your name\n"
					"- /timeout <num> - sets the timeout duration after the first person finishes\n"
					"- /color <color> - changes your player color. one of blue, green, cyan, red, magenta, yellow, or white\n"
				);
				lobby.log.draw().refresh();
			}
		},
		Command{
			"ready",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (!lobby.players.get_self().ready) {
					lobby.socket
						<< uint8_t( 0 )
						<< Socket::FLUSH;
				}
			}
		},
		Command{
			"unready",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (lobby.players.get_self().ready) {
					lobby.socket
						<< uint8_t( 0 )
						<< Socket::FLUSH;
				}
			}
		},
		Command{
			"spectate",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (!lobby.players.get_self().spectate) {
					lobby.socket
						<< uint8_t( 6 ) 
						<< Socket::FLUSH;
				}
			}
		},
		Command{
			"partake",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (lobby.players.get_self().spectate) {
					lobby.socket
						<< uint8_t( 6 )
						<< Socket::FLUSH;
				}
			}
		},
		Command{
			"name",
			[&lobby](const std::vector<std::string> & args) -> void {
				auto name = std::string();
				for (auto i = 0; i < args.size(); ++i) {
					name.append(args[i]);
					if (i < args.size() - 1) {
						name.append(1, ' ');
					}
				}
				if (name.size() > 0xFF) {
					lobby.log.message(COLOR_RED_B, "Sorry. Max name length is 255.");
					lobby.log.draw().refresh();
				} else {
					lobby.socket
						<< uint8_t( 0x01 )
						<< uint8_t( name.size() )
						<< name
						<< Socket::FLUSH;
				}
			}
		},
		Command{
			"timeout",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (args.size() < 1) {
					lobby.log.message(COLOR_RED_B, "/timeout expects an argument");
					lobby.log.draw().refresh();
					return;
				}

				auto num = 0;
				auto & text = args[0];
				if (text.size() > 6) {
					lobby.log.message(COLOR_RED_B, "/timeout requires a number between 0 and 32767 inclusive");
					lobby.log.draw().refresh();
					return;
				}
				for (auto i = 0; i < text.size(); ++i) {
					if (text[i] < '0' || text[i] > '9') {
						lobby.log.message(COLOR_RED_B, "/timeout expects a number");
						lobby.log.draw().refresh();
						return;
					}

					num *= 10;
					num += text[i] - '0';
				}
				if (num > 32767) {
					lobby.log.message(COLOR_RED_B, "/timeout requires a number between 0 and 32767 inclusive");
					lobby.log.draw().refresh();
					return;
				}
				lobby.socket
					<< uint8_t( 0x09 )
					<< uint16_t( num )
					<< Socket::FLUSH;
			}
		},
		Command{
			"killme",
			[&lobby](const std::vector<std::string> & args) -> void {
				lobby.socket
					<< uint8_t( 0x08 )
					<< uint8_t( 0x00 )
					<< uint8_t( lobby.players.get_self().id )
					<< Socket::FLUSH;
			}
		},
		Command{
			"special",
			[&lobby](const std::vector<std::string> & args) -> void {
				lobby.socket
					<< uint8_t( 0x08 )
					<< uint8_t( 0x01 );
				if (args[0] == "66") {
					lobby.socket << uint16_t( 66 );
				} else if (args[0] == "473") {
					lobby.socket << uint16_t( 473 );
				} else if (args[0] == "568") {
					lobby.socket << uint16_t( 568 );
				} else {
					lobby.socket.clear();
					lobby.log.message(std::string("Unrecognized special text '").append(args[0]).append("'"));
					lobby.log.draw().refresh();
				}
				lobby.socket << Socket::FLUSH;
			}
		},
		Command{
			"color",
			[&lobby](const std::vector<std::string> & args) -> void {
				if (args.size() != 1) {
					lobby.log.message(COLOR_RED_B, "/color expects one argument");
					lobby.log.draw().refresh();
					return;
				}	
				auto color = get_color(args[0]);
				if (color == -1) {
					lobby.log.message(COLOR_RED_B, "/color expects one of blue, green, cyan, red, magenta, yellow, or white");
					lobby.log.draw().refresh();
					return;
				}
				lobby.socket
					<< uint8_t( 0x0A )
					<< uint8_t( color )
					<< Socket::FLUSH;
			}
		}
	};

	lobby.root.draw().refresh();

	while (quit == CONNECTED) {
		if (lobby.command.update()) {
			if (lobby.command.complete()) {
				dispatch_command(lobby.command.get_and_clear(), commands, lobby.log);
			}
			if (lobby.command.height_change(lobby.command.height() + 1)) {
				lobby.split3.component_resize();
				lobby.log_box.draw();
				lobby.command_box.draw();
				lobby.split3.refresh();
			} else {
				lobby.command.draw().refresh();
			}
			lobby.split3.refresh();
		}
		if (quit == CONNECTED) {
			constexpr static uint8_t
				CONNECT        = 0x00,
				TOGGLE_READY   = 0x01,
				DISCONNECT     = 0x02,
				START          = 0x03,
				UPDATE_NAME    = 0x08,
				RELAY_MESSAGE  = 0x09,
				RELAY_SPECTATE = 0x0A,
				BEGIN_TIMER    = 0x0B,
				CANCEL_TIMER   = 0x0C,
				TEXT_SET       = 0x0D,
				UPDATE_TIMEOUT = 0x0E,
				UPDATE_COLOR   = 0x0F;
			if (lobby.socket.poll(0)) {
				auto read = lobby.socket.read();
				switch (read) {
					case CONNECT: {
						auto id    = uint32_t( lobby.socket.read() );
						auto color = lobby.socket.read();
						auto name  = std::string();
						lobby.socket.width(Socket::U8) >> name;
						if (id != lobby.players.get_self().id) {
							lobby.log.message(std::string("Player '").append(name).append("' connected"));
							lobby.players.add_player(id, color, false, false, name);
							lobby.players.draw().refresh();
							lobby.log.draw().refresh();
						}
						break;
					}
					case TOGGLE_READY: {
						auto id = uint32_t( lobby.socket.read() );
						auto & player = lobby.players.get_player(id);
						player.ready = !player.ready;
						if (player.ready) {
							lobby.log.message(std::string("Player '").append(player.name).append("' is ready"));
						} else {
							lobby.log.message(std::string("Player '").append(player.name).append("' is no longer ready"));
						}
						lobby.players.draw().refresh();
						lobby.log.draw().refresh();
						break;
					}
					case DISCONNECT: {
						auto id = uint32_t( lobby.socket.read() );
						lobby.log.message(std::string("Player '").append(lobby.players.get_player(id).name).append("' disconnected"));
						lobby.players.remove_player(id);
						lobby.players.draw().refresh();
						lobby.log.draw().refresh();
						break;
					}
					case START: {
						file << "Game is starting..." << std::endl;
						quit = PLAYING;
						break;
					}
					case UPDATE_NAME: {
						auto id = uint32_t( lobby.socket.read() );
						auto & player = lobby.players.get_player(id);
						auto new_name = std::string();
						lobby.socket.width(Socket::U8) >> new_name;
						file << "New Name: " << new_name << std::endl;
						lobby.log.message(std::string("Player '").append(player.name).append("' has changed their name to '").append(player.name = new_name).append("'"));
						lobby.log.draw().refresh();
						lobby.players.draw().refresh();
						break;
					}
					case RELAY_MESSAGE: {
						auto id = uint32_t( lobby.socket.read() );
						auto & player = lobby.players.get_player(id);
						auto message = std::string();
						lobby.socket.width(Socket::U16) >> message;
						lobby.log.message(player.color, player.name, message);
						lobby.log.draw().refresh();
						break;
					}
					case RELAY_SPECTATE: {
						auto id = uint32_t( lobby.socket.read() );
						auto & player   = lobby.players.get_player(id);
						player.spectate = !player.spectate;
						player.ready    = false;
						if (player.spectate) {
							lobby.log.message(std::string("Player '").append(player.name).append("' is now a spectator"));
						} else {
							lobby.log.message(std::string("Player '").append(player.name).append("' is no longer a spectator"));
						}
						lobby.players.draw().refresh();
						lobby.log.draw().refresh();
						break;
					}
					case BEGIN_TIMER: {
						auto seconds = uint32_t( lobby.socket.read() );
						lobby.log.message(std::string("Game will begin in ").append(std::to_string(seconds)).append(" seconds..."));
						lobby.log.draw().refresh();
						break;
					}
					case CANCEL_TIMER: {
						lobby.log.message("Game start has been cancelled");
						lobby.log.draw().refresh();
						break;
					}
					case TEXT_SET: {
						read = lobby.socket.read();
						switch (read) {
							case 0: {
								auto & player = lobby.players.get_player(lobby.socket.read());
								lobby.log.message("Opens fridge...");
								lobby.log.message(player.color, player.name, "Kill me");
								lobby.log.message(COLOR_RED_B, "Medic", "Later");
								lobby.log.draw().refresh();
								break;
							}
							case 1: {
								auto selection = lobby.socket.read16();
								file << "Selection: " << selection << std::endl;
								switch (selection) {
									case 66: {
										lobby.log.message("A plague is a approaching...");
										break;
									}
									case 473: {
										lobby.log.message("A deceased arm looms overhead...");
										break;
									}
									case 568: {
										lobby.log.message("Carrot 2 bios update pending...");
										break;
									}
								}
								lobby.log.draw().refresh();
								break;
							}
						}
						break;
					}
					case UPDATE_TIMEOUT: {
						auto time = lobby.socket.read16();
						lobby.log.message(std::string("Timeout changed to ").append(std::to_string(time)).append(" seconds"));
						lobby.log.draw().refresh();
						break;
					}
					case UPDATE_COLOR: {
						auto id = lobby.socket.read();
						auto color = lobby.socket.read();
						lobby.players.get_player(id).color = color;
						lobby.players.draw().refresh();
						break;
					}
					default: {
						file << "Wakarimasen deshita " << uint32_t( read ) << std::endl;
					}
				}
			}
		}
		lobby.command.move_cursor();
		std::this_thread::sleep_for(std::chrono::milliseconds(15));
	}
	return quit;
}

auto playing(LobbyState & lobby, bool newly_connected) -> uint32_t {
	Playing playing{ lobby.root };
	auto victor      = -1;
	auto victor_time = -1;

	auto & players = playing.get_players();
	if (newly_connected) {
		auto my_id  = lobby.socket.read();
		victor      = lobby.socket.read();
		victor_time = lobby.socket.read32();
		auto text = std::string();
		lobby.socket.width(Socket::U16) >> text;
		auto & self = lobby.players.get_self();
		self.id = my_id;
		playing.set(text, my_id, self.name, self.color, true);
		auto player_count = lobby.socket.read();
		for (auto i = uint32_t( 0 ); i < player_count; ++i) {
			auto id = lobby.socket.read();
			auto spectator = lobby.socket.read();
			auto color     = lobby.socket.read();
			auto name = std::string();
			lobby.socket.width(Socket::U8) >> name;
			auto progress = uint32_t(lobby.socket.read16());
			auto finish_time = lobby.socket.read32();
			if (id != my_id) {
				players.push_back({ id, name, color, spectator, 
					progress == text.size() ?
						finish_time :
						progress
				});
			}
		}
	} else {
		auto text = std::string();
		lobby.socket.width(Socket::U16) >> text;
		auto & self = lobby.players.get_self();
		playing.set(text, self.id, self.name, self.color, self.spectate);
		for (auto i = 0; i < lobby.players.length() - 1; ++i) {
			auto & player = lobby.players.get_player_index(i + 1);
			players.push_back({ player.id, player.name, player.color, player.spectate, 0 });
		}
	}

	lobby.split.set_second_child(&playing);
	lobby.root.draw().refresh();
	
	auto complete = false;
	auto last_progress = uint32_t( 0 );

	auto quit = PLAYING;
	while (quit == PLAYING) {
		auto c = uint32_t( 0 );
		if ((c = playing.get_char()) != ERR) {
			playing.feed_char(c);
			if (!complete) {
				if (playing.completed()) {
					lobby.socket
						<< uint8_t( 0x03 )
						<< playing.get_time()
						<< Socket::FLUSH;
					complete = true;
				} else if ((c = playing.get_progress()) != last_progress) {
					lobby.socket
						<<  uint8_t( 0x02 )
						<< uint16_t( c    )
						<< Socket::FLUSH;
					last_progress = c;
				}
			}
		}
		constexpr static uint8_t
			CONNECT          = 0x00,
			UPDATE_PROGRESS  = 0x04,
			PLAYER_COMPLETED = 0x05,
			DISCONNECT       = 0x02,
			GAME_OVER        = 0x07;
		if (lobby.socket.poll(0)) {
			auto read = lobby.socket.read();
			switch (read) {
				case CONNECT: {
					auto id    = uint32_t( lobby.socket.read() );
					auto color = lobby.socket.read();
					auto name  = std::string();
					lobby.socket.width(Socket::U8) >> name;
					if (id != lobby.players.get_self().id) {
						lobby.log.message(std::string("Player '").append(name).append("' connected"));
						playing.get_players().push_back({ id, name, color, true, 0 });
						lobby.players.draw().refresh();
						lobby.log.draw().refresh();
					}
					break;
				}
				case UPDATE_PROGRESS: {
					auto id = uint32_t( lobby.socket.read() );
					auto progress = lobby.socket.read16();
					if (id != lobby.players.get_self().id) {
						auto & player = playing.get_player(id);
						player.progress = progress;
					}
					break;
				}
				case PLAYER_COMPLETED: {
					auto id = uint32_t( lobby.socket.read() );
					auto time = lobby.socket.read32();
					if (victor == -1 || time < victor_time) {
						victor = id;
						victor_time = time;
					}
					auto & player = playing.get_player(id);
					player.status   = Playing::COMPLETED;
					player.progress = time;
					break;
				}
				case DISCONNECT: {
					auto id = uint32_t( lobby.socket.read() );
					auto & player = playing.get_player(id);
					player.status = Playing::DISCONNECTED;
					if (id == victor) {
						victor      = -1;
						victor_time = -1;
					}
					lobby.log.message(std::string("Player '").append(playing.get_player(id).name).append("' disconnected"));
					break;
				}
				case GAME_OVER: {
					quit = LOBBY;
					if (victor != -1) {
						lobby.log.message(COLOR_GREEN_B, std::string("Player '").append(playing.get_player(victor).name).append("' won the game!"));
					} else {
						lobby.log.message("You have all failed!");
					}
					if (!playing.completed()) {
						playing.finalize();
					}
					auto & player = playing.get_self();
					if (player.status != Playing::SPECTATOR) {
						lobby.log.message(
							std::string("--- Performance report ---"
							"\nCPM: ").append(std::to_string(playing.get_cpm())).append(
							"\nWPM: ").append(std::to_string(playing.get_wpm())).append(
							"\nTotal Time: ").append(std::to_string(playing.get_time() / 1000000)).append(" seconds")
						);
					}
					break;
				}
				default: {
					file << "Wakarimasen 2 deshita: " << read << std::endl;
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(15));
		playing.draw().refresh();
	}

	{
		auto & self         = lobby.players.get_self();
		auto & playing_self = playing.get_self();
		self.id       = playing_self.id;
		self.color    = playing_self.color;
		self.ready    = false;
		self.spectate = playing_self.status == Playing::SPECTATOR;
		self.name     = playing_self.name;
		lobby.players.clear_list();
		for (auto & player : playing.get_players()) {
			if (player.status != Playing::DISCONNECTED) {
				lobby.players.add_player(player.id, player.color, false, player.status == Playing::SPECTATOR, player.name);
			}
		}
	}

	lobby.split.set_second_child(&lobby.split2);
	lobby.root.draw().refresh();

	return quit;
}

auto dispatch_command(const std::string & raw, const std::vector<Command> & commands, Log & error) -> void {
	file << "Dispatching: " << raw << std::endl;
	if (raw.size() > 0) {
		if (raw[0] != '/') {
			commands[0].callback(std::vector<std::string>{ raw });
		} else if (raw.size() > 1 && raw[1] == '/') {
			commands[0].callback(std::vector<std::string>{ raw.substr(1, raw.size() - 1) });
		} else {
			auto end = 0;
			while (++end < raw.size() && raw[end] != ' ');

			auto i = 0;
			for (; i < commands.size(); ++i) {
				if (commands[i].matches(raw, end)) {
					commands[i].callback(get_args(end + 1, raw));
					break;
				}
			}
			
			if (i == commands.size()) {
				auto error_message = std::string("Undefined command '").append(raw, 0, end).append(1, '\'');
				error.message(COLOR_RED_B, error_message);
				error.draw().refresh();
			}
		}
	}
}

auto get_args(int32_t start, const std::string & raw) -> std::vector<std::string> {
	auto args = std::vector<std::string>{};
	auto quoted = false;
	auto consume_next = false;
	auto arg = std::string();
	for (auto i = start; i < raw.size(); ++i) {
		if (consume_next) {
			arg += raw[i];
			consume_next = false;
		} else if (raw[i] == '\\') {
			consume_next = true;
		} else if (raw[i] == '"') {
			quoted = !quoted;
		} else if ((raw[i] == ' ' || raw[i] == '\n' || raw[i] == '\t') && !quoted) {
			if (arg.size() > 0) {
				args.push_back(std::move(arg));
				arg = std::string();
			}
		} else {
			arg += raw[i];
		}
	}
	if (arg.size() > 0) {
		args.push_back(std::move(arg));
	}
	return args;
}

auto get_color(std::string color) -> int8_t {
	std::transform(color.begin(), color.end(), color.begin(),
		[](unsigned char c){ return std::tolower(c); }
	);
	     if (color == "blue")    { return COLOR_BLUE_B;    }
	else if (color == "green")   { return COLOR_GREEN_B;   }
	else if (color == "cyan")    { return COLOR_CYAN_B;    }
	else if (color == "red")     { return COLOR_RED_B;     }
	else if (color == "magenta") { return COLOR_MAGENTA_B; }
	else if (color == "yellow")  { return COLOR_YELLOW_B;  }
	else if (color == "white")   { return COLOR_WHITE_B;   }
	else                         { return -1;              }
}

auto get_ip(const std::string & ip_string) -> in_addr_t {
	auto hints = addrinfo{ 0 };
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	auto * info = (addrinfo*) 0;
	getaddrinfo(ip_string.c_str(), NULL, &hints, &info);

	auto ret = ((sockaddr_in*) info->ai_addr)->sin_addr.s_addr;

	freeaddrinfo(info);

	return ret;
}

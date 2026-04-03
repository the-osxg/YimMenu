#include "osxg_api.hpp"
#include <shellapi.h>
#include "lua/sol_include.hpp"
#include "services/players/player_service.hpp"
#include "util/session.hpp"
#include "core/enums.hpp"
#include "http_client/http_client.hpp"
#include "logger/logger.hpp"
#include "fiber_pool.hpp"
#include <nlohmann/json.hpp>

namespace osxg
{
	static sol::object nlohmann_to_sol(const nlohmann::json& j, sol::state_view& lua)
	{
		switch (j.type())
		{
		case nlohmann::json::value_t::object:
		{
			sol::table t = lua.create_table();
			for (auto& el : j.items())
				t[el.key()] = nlohmann_to_sol(el.value(), lua);
			return t;
		}
		case nlohmann::json::value_t::array:
		{
			sol::table t = lua.create_table();
			int i        = 1;
			for (auto& el : j)
				t[i++] = nlohmann_to_sol(el, lua);
			return t;
		}
		case nlohmann::json::value_t::string: return sol::make_object(lua, j.get<std::string>());
		case nlohmann::json::value_t::number_integer: return sol::make_object(lua, j.get<long long>());
		case nlohmann::json::value_t::number_unsigned: return sol::make_object(lua, j.get<unsigned long long>());
		case nlohmann::json::value_t::number_float: return sol::make_object(lua, j.get<double>());
		case nlohmann::json::value_t::boolean: return sol::make_object(lua, j.get<bool>());
		default: return sol::nil;
		}
	}

	static nlohmann::json sol_to_nlohmann(sol::object obj)
	{
		switch (obj.get_type())
		{
		case sol::type::boolean: return obj.as<bool>();
		case sol::type::number: return obj.as<double>();
		case sol::type::string: return obj.as<std::string>();
		case sol::type::table:
		{
			sol::table t = obj.as<sol::table>();
			bool is_array = true;
			size_t max_index = 0;
			t.for_each([&](sol::object key, sol::object value) {
				if (key.get_type() != sol::type::number) is_array = false;
				else max_index = std::max(max_index, (size_t)key.as<double>());
			});

			if (is_array && max_index == t.size())
			{
				nlohmann::json j = nlohmann::json::array();
				for (size_t i = 1; i <= t.size(); ++i)
					j.push_back(sol_to_nlohmann(t[i]));
				return j;
			}
			else
			{
				nlohmann::json j = nlohmann::json::object();
				t.for_each([&](sol::object key, sol::object value) {
					if (key.get_type() == sol::type::string)
						j[key.as<std::string>()] = sol_to_nlohmann(value);
				});
				return j;
			}
		}
		default: return nullptr;
		}
	}

	static uint64_t get_local_rockstar_id()
	{
		if (auto self = big::g_player_service->get_self(); self && self->is_valid())
			return (uint64_t)self->get_rockstar_id();
		return 0;
	}

	static uint64_t get_player_rockstar_id(int pid)
	{
		if (auto plyr = big::g_player_service->get_by_id(pid); plyr && plyr->is_valid())
			return (uint64_t)plyr->get_rockstar_id();
		return 0;
	}

	static std::string get_player_name(int pid)
	{
		if (auto plyr = big::g_player_service->get_by_id(pid); plyr && plyr->is_valid())
			return std::string(plyr->get_name());
		return "Unknown";
	}

	static std::string get_local_player_name()
	{
		if (auto self = big::g_player_service->get_self(); self && self->is_valid())
		{
			const char* name = self->get_name();
			if (name && strlen(name) > 0)
				return std::string(name);
		}
		return "Unknown";
	}

	static void open_url(const std::string& url)
	{
		ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
	}

	static void join_session_by_rockstar_id(uint64_t rid)
	{
		big::g_fiber_pool->queue_job([rid]() {
			big::session::join_by_rockstar_id(rid);
		});
	}

	static void invite_by_rockstar_id(uint64_t rid)
	{
		big::g_fiber_pool->queue_job([rid]() {
			big::session::invite_by_rockstar_id(rid);
		});
	}

	static std::string get_local_session_info()
	{
		if (!big::gta_util::get_network() || !big::g_pointers->m_gta.m_encode_session_info)
			return "";

		char buf[0x100]{};
		if (big::g_pointers->m_gta.m_encode_session_info(&big::gta_util::get_network()->m_last_joined_session, buf, 0xA9, nullptr))
			return std::string(buf);
		return "";
	}

	static void join_session_by_info(const std::string& b64)
	{
		if (b64.empty()) return;
		big::g_fiber_pool->queue_job([b64]() {
			rage::rlSessionInfo info;
			std::vector<char> buf(b64.begin(), b64.end());
			buf.push_back('\0');
			if (big::g_pointers->m_gta.m_decode_session_info(&info, buf.data(), nullptr))
				big::session::join_session(info);
			else
				LOG(WARNING) << "[OSXG] Failed to decode session info.";
		});
	}

	static void create_public_session()
	{
		big::g_fiber_pool->queue_job([]() {
			big::session::join_type(big::eSessionType::NEW_PUBLIC);
		});
	}

	static sol::object http_get(const std::string& url, sol::table headers, sol::this_state s)
	{
		sol::state_view lua(s);
		cpr::Header cpr_headers;
		for (auto& it : headers)
		{
			if (it.first.is<std::string>() && it.second.is<std::string>())
				cpr_headers.emplace(it.first.as<std::string>(), it.second.as<std::string>());
		}

		auto res = big::g_http_client.get(cpr::Url{url}, cpr_headers);

		sol::table response = lua.create_table();
		response["status"] = res.status_code;
		response["body"] = res.text;
		return response;
	}

	static sol::object http_post(const std::string& url, sol::table headers, const std::string& body, sol::this_state s)
	{
		sol::state_view lua(s);
		cpr::Header cpr_headers;
		for (auto& it : headers)
		{
			if (it.first.is<std::string>() && it.second.is<std::string>())
				cpr_headers.emplace(it.first.as<std::string>(), it.second.as<std::string>());
		}

		auto res = big::g_http_client.post(cpr::Url{url}, cpr_headers, cpr::Body{body});

		sol::table response = lua.create_table();
		response["status"] = res.status_code;
		response["body"] = res.text;
		return response;
	}

	static sol::object json_parse(const std::string& json_str, sol::this_state s)
	{
		sol::state_view lua(s);
		try
		{
			auto j = nlohmann::json::parse(json_str);
			return nlohmann_to_sol(j, lua);
		}
		catch (...)
		{
			return sol::nil;
		}
	}

	static std::string json_stringify(sol::object obj)
	{
		try
		{
			auto j = sol_to_nlohmann(obj);
			return j.dump();
		}
		catch (...)
		{
			return "{}";
		}
	}

	void bind(sol::state& state)
	{
		auto ns = state["osxg"].get_or_create<sol::table>();
		ns["get_local_rockstar_id"] = get_local_rockstar_id;
		ns["get_player_rockstar_id"] = get_player_rockstar_id;
		ns["join_session_by_rockstar_id"] = join_session_by_rockstar_id;
		ns["create_public_session"] = create_public_session;
		ns["http_get"] = http_get;
		ns["http_post"] = http_post;
		ns["json_parse"] = json_parse;
		ns["json_stringify"] = json_stringify;
		ns["get_player_name"] = get_player_name;
		ns["get_local_player_name"] = get_local_player_name;
		ns["get_local_session_info"] = get_local_session_info;
		ns["join_session_by_info"] = join_session_by_info;
		ns["invite_by_rockstar_id"] = invite_by_rockstar_id;
		ns["open_url"] = open_url;
	}
}

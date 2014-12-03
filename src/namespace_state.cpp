#include "namespace_state_p.hpp"
#include "utils.hpp"

#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <cstdlib>
#include <sstream>

mastermind::namespace_state_t::user_settings_t::~user_settings_t() {
}

mastermind::namespace_state_t::data_t::settings_t::settings_t(const kora::config_t &state
		, const user_settings_factory_t &factory)
	try
	: groups_count(state.at<size_t>("groups-count"))
	, success_copies_num(state.at<std::string>("success-copies-num"))
{
	if (state.has("auth-keys")) {
		const auto &auth_keys_state = state.at("auth-keys");

		auth_keys.read = auth_keys_state.at<std::string>("read", "");
		auth_keys.read = auth_keys_state.at<std::string>("write", "");
	}

	if (factory) {
		user_settings_ptr = factory(state);
	}
} catch (const std::exception &ex) {
	throw std::runtime_error(std::string("cannot create settings-state: ") + ex.what());
}

mastermind::namespace_state_t::data_t::settings_t::settings_t(settings_t &&other)
	: groups_count(other.groups_count)
	, success_copies_num(std::move(other.success_copies_num))
	, auth_keys(std::move(other.auth_keys))
	, user_settings_ptr(std::move(other.user_settings_ptr))
{
}

mastermind::namespace_state_t::data_t::couples_t::couples_t(const kora::config_t &state)
	try
{
	for (size_t index = 0, size = state.size(); index != size; ++index) {
		const auto &couple_info_state = state.at(index);

		auto couple_id = couple_info_state.at<std::string>("id");

		auto ci_insert_result = couple_info_map.insert(std::make_pair(
					couple_id, couple_info_t()));

		if (std::get<1>(ci_insert_result) == false) {
			throw std::runtime_error("reuse the same couple_id=" + couple_id);
		}

		auto &couple_info = std::get<0>(ci_insert_result)->second;

		couple_info.id = couple_id;

		{
			const auto &dynamic_tuple = couple_info_state.at("tuple")
				.underlying_object().as_array();

			for (auto it = dynamic_tuple.begin(), end = dynamic_tuple.end();
					it != end; ++it) {
				couple_info.groups.emplace_back(it->to<group_t>());
			}
		}

		{
			auto status = couple_info_state.at<std::string>("couple_status");

			if (status == "BAD") {
				couple_info.status = couple_info_t::status_tag::BAD;
			} else {
				couple_info.status = couple_info_t::status_tag::UNKNOWN;
			}
		}

		couple_info.free_effective_space = couple_info_state.at<uint64_t>("free_effective_space");

		const auto &groups_info_state = couple_info_state.at("groups");

		for (size_t index = 0, size = groups_info_state.size(); index != size; ++index) {
			const auto &group_info_state = groups_info_state.at(index);

			auto group_id = group_info_state.at<group_t>("id");

			auto gi_insert_result = group_info_map.insert(std::make_pair(
						group_id, group_info_t()));

			if (std::get<1>(gi_insert_result) == false) {
				throw std::runtime_error("resuse the same group_id="
						+ boost::lexical_cast<std::string>(group_id));
			}

			auto &group_info = std::get<0>(gi_insert_result)->second;

			group_info.id = group_id;

			{
				auto status = group_info_state.at<std::string>("status");

				if (status == "COUPLED") {
					group_info.status = group_info_t::status_tag::COUPLED;
				} else {
					group_info.status = group_info_t::status_tag::UNKNOWN;
				}
			}

			group_info.couple_info_map_iterator = std::get<0>(ci_insert_result);
			couple_info.groups_info_map_iterator.emplace_back(std::get<0>(gi_insert_result));
		}
	}
} catch (const std::exception &ex) {
	throw std::runtime_error(std::string("cannot create couples-state: ") + ex.what());
}

mastermind::namespace_state_t::data_t::weights_t::weights_t(const kora::config_t &config
		, size_t groups_count_)
	try
	: groups_count(groups_count_)
	, couples_with_info(create_couples_with_info(config, groups_count))
	, couples_by_avalible_memory(create_couples_with_info(couples_with_info))

{
} catch (const std::exception &ex) {
	throw std::runtime_error(std::string("cannot create weights-state: ") + ex.what());
}

mastermind::namespace_state_t::data_t::weights_t::couples_with_info_t
mastermind::namespace_state_t::data_t::weights_t::create_couples_with_info(
		const kora::config_t &config, size_t groups_count) {
	const auto &couples = config.at(boost::lexical_cast<std::string>(groups_count))
		.underlying_object().as_array();

	couples_with_info_t couples_with_info;

	for (auto it = couples.begin(), end = couples.end(); it != end; ++it) {
		const auto &couple = it->as_array();

		groups_t groups;

		{
			const auto &dynamic_groups = couple[0].as_array();

			for (auto it = dynamic_groups.begin(), end = dynamic_groups.end();
					it != end; ++it) {
				groups.emplace_back(it->to<group_t>());
			}
		}

		if (groups.size() != groups_count) {
			std::ostringstream oss;
			oss
				<< "groups.size is not equeal for groups_count(" << groups_count
				<< "), groups=" << groups;
			throw std::runtime_error(oss.str());
		}

		couples_with_info.emplace_back(std::make_tuple(groups
					, couple[1].to<uint64_t>(), couple[2].to<uint64_t>()));
	}

	std::sort(couples_with_info.begin(), couples_with_info.end(), couples_with_info_cmp);

	return couples_with_info;
}

mastermind::namespace_state_t::data_t::weights_t::couples_by_avalible_memory_t
mastermind::namespace_state_t::data_t::weights_t::create_couples_with_info(
		const couples_with_info_t &couples_with_info) {
	couples_by_avalible_memory_t couples_by_avalible_memory;

	for (size_t index = 0; index != couples_with_info.size(); ++index) {
		auto avalible_memory = std::get<2>(couples_with_info[index]);
		uint64_t total_weight = 0;

		for (size_t index2 = 0; index2 <= index; ++index2) {
			const auto &couple_with_info = couples_with_info[index2];

			auto weight = std::get<1>(couple_with_info);

			if (weight == 0) {
				continue;
			}

			total_weight += weight;

			couples_by_avalible_memory[avalible_memory].insert(
				std::make_pair(total_weight, index2)
			);
		}
	}

	return couples_by_avalible_memory;
}

mastermind::namespace_state_t::data_t::weights_t::couple_with_info_t
mastermind::namespace_state_t::data_t::weights_t::get(size_t groups_count_
		, uint64_t size) const {
	if (groups_count_ != groups_count) {
		throw invalid_groups_count_error();
	}

	auto amit = couples_by_avalible_memory.lower_bound(size);
	if (amit == couples_by_avalible_memory.end()) {
		throw not_enough_memory_error();
	}

	auto &weighted_groups = amit->second;
	if (weighted_groups.empty()) {
		throw couple_not_found_error();
	}

	auto total_weight = weighted_groups.rbegin()->first;
	double shoot_point = double(random()) / RAND_MAX * total_weight;
	auto it = weighted_groups.lower_bound(uint64_t(shoot_point));

	if (it == weighted_groups.end()) {
		throw couple_not_found_error();
	}

	return couples_with_info[it->second];
}

const mastermind::namespace_state_t::data_t::weights_t::couples_with_info_t &
mastermind::namespace_state_t::data_t::weights_t::data() const {
	return couples_with_info;
}

bool
mastermind::namespace_state_t::data_t::weights_t::couples_with_info_cmp(
		const couple_with_info_t &lhs, const couple_with_info_t &rhs) {
	return std::get<2>(lhs) > std::get<2>(rhs);
}


mastermind::namespace_state_t::data_t::statistics_t::statistics_t(const kora::config_t &state)
	try
{
} catch (const std::exception &ex) {
	throw std::runtime_error(std::string("cannot create statistics-state: ") + ex.what());
}

mastermind::namespace_state_t::data_t::data_t(std::string name_, const kora::config_t &config
		, const user_settings_factory_t &factory)
	try
	: name(std::move(name_))
	, settings(config.at("settings"), factory)
	, couples(config.at("couples"))
	, weights(config.at("weights"), settings.groups_count)
	, statistics(config.at("statistics"))
{
	check_consistency();

} catch (const std::exception &ex) {
	throw std::runtime_error("cannot create ns-state " + name + ": " + ex.what());
}

mastermind::namespace_state_t::data_t::data_t(data_t &&other)
	: name(std::move(other.name))
	, settings(std::move(other.settings))
	, couples(std::move(other.couples))
	, weights(std::move(other.weights))
	, statistics(std::move(other.statistics))
	, extract(std::move(other.extract))
{
}

void
mastermind::namespace_state_t::data_t::check_consistency() {
	std::ostringstream oss;

	oss << "namespace=" << name;
	oss << " groups-count=" << settings.groups_count;

	{
		if (couples.couple_info_map.empty()) {
			throw std::runtime_error("couples list is empty");
		}
	}

	{
		size_t nonzero_weights = 0;

		const auto &weights_data = weights.data();

		for (auto it = weights_data.begin(), end = weights_data.end(); it != end; ++it) {
			auto weight = std::get<1>(*it);

			if (weight != 0) {
				nonzero_weights += 1;
			}

			{
				auto couple_it = couples.couple_info_map.cend();
				const auto &groups = std::get<0>(*it);

				for (auto git = groups.begin(), gend = groups.end(); git != gend; ++git) {
					auto group_info_it = couples.group_info_map.find(*git);

					if (group_info_it == couples.group_info_map.end()) {
						throw std::runtime_error("there is no group-info for group "
								+ boost::lexical_cast<std::string>(*git));
					}

					if (couple_it != couples.couple_info_map.end()) {
						if (couple_it != group_info_it->second.couple_info_map_iterator) {
							std::ostringstream oss;
							oss << "inconsisten couple: " << groups;
							throw std::runtime_error(oss.str());
						}
					} else {
						couple_it = group_info_it->second.couple_info_map_iterator;
					}
				}

				if (couple_it->second.groups.size() != groups.size()) {
					std::ostringstream oss;
					oss << "inconsisten couple: " << groups;
					throw std::runtime_error(oss.str());
				}
			}
		}

		if (nonzero_weights == 0) {
			throw std::runtime_error("no weighted coulples were obtained from mastermind");
		}

		oss << " couples-for-write=" << nonzero_weights;
	}

	oss << " couples=" << couples.couple_info_map.size();

	extract = oss.str();
}

mastermind::namespace_state_t::namespace_state_t() {
}

mastermind::namespace_state_init_t::namespace_state_init_t(
		std::shared_ptr<const namespace_state_t::data_t> data_) {
	data = std::move(data_);
}

mastermind::namespace_state_init_t::data_t::data_t(std::string name
		, const kora::config_t &config, const user_settings_factory_t &factory)
	: namespace_state_t::data_t(std::move(name), config, factory)
{
}

mastermind::namespace_state_init_t::data_t::data_t(data_t &&other)
	: namespace_state_t::data_t(std::move(other))
{
}

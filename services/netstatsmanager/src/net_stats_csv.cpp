/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>
#include <vector>

#include "net_stats_csv.h"
#include "parse_csv_int.h"

#include "net_mgr_log_wrapper.h"

#include "netd_controller.h"

namespace OHOS {
namespace NetManagerStandard {
const std::string CSV_DIR = "/data/data/";
const std::string IFACE_CSV_FILE_NAME = "iface.csv";
const std::string UID_CSV_FILE_NAME = "uid.csv";
const std::string IFACE_STATS_CSV_FILE_NAME = "iface_stats.csv";
const std::string IFACE_STATS_CSV_BAK = "iface_stats_bak";
const std::string IFACE_STATS_CSV_NEW = "iface_stats_new";
const std::string UID_STATS_CSV_FILE_NAME = "uid_stats.csv";

static std::istream& operator>>(std::istream& str, CSVRow& data)
{
    data.ReadNextRow(str);
    return str;
}

void CSVRow::ReadNextRow(std::istream& str)
{
    std::string line;
    std::getline(str, line);

    std::stringstream lineStream(line);
    std::string cell;

    data_.clear();
    while (std::getline(lineStream, cell, ',')) {
        data_.push_back(cell);
    }

    if (!lineStream && cell.empty()) {
        data_.push_back("");
    }
}

NetStatsCsv::NetStatsCsv()
{}

NetStatsCsv::~NetStatsCsv() {}

bool NetStatsCsv::ExistsIface(const std::string& iface)
{
    if (iface.empty())
        return false;

    std::ifstream ifaceCsvFile(CSV_DIR + IFACE_CSV_FILE_NAME);
    if (!ifaceCsvFile) {
        NETMGR_LOG_E("ifstream failed");
        return false;
    }

    CSVRow row;
    while (ifaceCsvFile >> row) {
        if (iface.compare(row[static_cast<uint32_t>(IfaceCsvColumn::IFACE_NAME)]) == 0)
            return true;
    }
    return false;
}

bool NetStatsCsv::ExistsUid(const uint32_t uid)
{
    std::ifstream uidCsvFile(CSV_DIR + UID_CSV_FILE_NAME);
    if (!uidCsvFile) {
        NETMGR_LOG_E("ifstream failed");
        return false;
    }

    CSVRow row;
    while (uidCsvFile >> row) {
        if (std::to_string(uid).compare(row[static_cast<uint32_t>(UidCsvColumn::UID)]) == 0)
            return true;
    }
    return false;
}

bool NetStatsCsv::RenameIfacesStatsCsv(const std::string& fromFileName)
{
    if (fromFileName.empty()) {
        NETMGR_LOG_E("fromFileName is empty");
        return false;
    }
    std::string bakIfaceStatsFileName = CSV_DIR + IFACE_STATS_CSV_BAK + "_" + GetCurrentTime();
    std::string toFileName = CSV_DIR + IFACE_STATS_CSV_FILE_NAME;
    std::lock_guard lock(mutex_);
    if (std::rename(toFileName.c_str(), bakIfaceStatsFileName.c_str())) {
        NETMGR_LOG_E("ofstream open failed");
        return false;
    }

    if (std::rename(fromFileName.c_str(), toFileName.c_str())) {
        NETMGR_LOG_E("ofstream open failed");
        return false;
    }
    return true;
}

bool NetStatsCsv::CorrectedIfacesStats(const std::string &iface,
    uint32_t start, uint32_t end, const NetStatsInfo &stats)
{
    uint32_t calStartTime = GetIfaceCalculateTime(iface, start);
    uint32_t calEndTime = GetIfaceCalculateTime(iface, end);
    if (calStartTime == calEndTime) {
        NETMGR_LOG_E("The end time must be greater than the start time, or the interval time cannot be too small.");
        return false;
    }

    std::ofstream newIfaceStatsCsvFile;
    std::string newIfaceStatsFileName = CSV_DIR + IFACE_STATS_CSV_NEW + "_" + GetCurrentTime();
    newIfaceStatsCsvFile.open(newIfaceStatsFileName, std::fstream::app);
    if (!newIfaceStatsCsvFile.is_open()) {
        NETMGR_LOG_E("ofstream open failed");
        return false;
    }

    CSVRow row;
    uint32_t rowTime = 0;
    bool correctedFlg = false;
    bool isWrited = false;
    std::ifstream iface_file(CSV_DIR + IFACE_STATS_CSV_FILE_NAME, std::fstream::in);
    while (iface_file >> row) {
        if (iface.compare(row[static_cast<uint32_t>(IfaceStatsCsvColumn::IFACE_NAME)]) == 0) {
            uint32_t timeCloumn = static_cast<uint32_t>(IfaceStatsCsvColumn::TIME);
            if (!ParseCsvUint32(row[(timeCloumn)], rowTime)) {
                continue;
            }
            if (calStartTime <= rowTime && rowTime  <= calEndTime) {
                correctedFlg = true;
                continue;
            }
            if (correctedFlg && !isWrited) {
                isWrited = true;
                newIfaceStatsCsvFile << iface << "," << start << ",0,0" << "" << std::endl;
                newIfaceStatsCsvFile << iface << "," << end << ","
                    << stats.rxBytes_ << "," << stats.txBytes_ << std::endl;
            }
        }
        newIfaceStatsCsvFile << iface << "," << row[static_cast<uint32_t>(IfaceStatsCsvColumn::TIME)] << ","
            << row[static_cast<uint32_t>(IfaceStatsCsvColumn::RXBYTES)] << ","
            << row[static_cast<uint32_t>(IfaceStatsCsvColumn::TXBYTES)] << std::endl;
    }
    iface_file.close();
    newIfaceStatsCsvFile.close();
    if (!RenameIfacesStatsCsv(newIfaceStatsFileName)) {
        return false;
    }
    return true;
}

bool NetStatsCsv::UpdateIfaceCsvInfo()
{
    std::fstream ifaceCsvFile;
    ifaceCsvFile.open(CSV_DIR + IFACE_CSV_FILE_NAME, std::fstream::out | std::fstream::trunc);
    if (!ifaceCsvFile.is_open()) {
        NETMGR_LOG_E("fstream open failed");
        ifaceCsvFile.close();
        return false;
    }
    std::vector<std::string> ifNameList = NetdController::GetInstance().InterfaceGetList();
    for (std::vector<std::string>::iterator iter = ifNameList.begin(); iter != ifNameList.end(); ++iter) {
        if (*iter != "lo") {
            ifaceCsvFile << *iter << std::endl;
        }
    }
    ifaceCsvFile.close();
    return true;
}

bool NetStatsCsv::UpdateUidCsvInfo()
{
    std::fstream uidCsvFile;
    uidCsvFile.open(CSV_DIR + UID_CSV_FILE_NAME, std::fstream::out | std::fstream::trunc);
    if (!uidCsvFile.is_open()) {
        NETMGR_LOG_E("fstream open failed");
        uidCsvFile.close();
        return false;
    }
    std::vector<std::string> uidList = NetdController::GetInstance().UidGetList();
    for (std::vector<std::string>::iterator iter = uidList.begin(); iter != uidList.end(); ++iter) {
        uidCsvFile << *iter << std::endl;
    }
    uidCsvFile.close();
    return true;
}

bool NetStatsCsv::UpdateIfaceStats()
{
    std::ifstream ifaceCsvFile(CSV_DIR + IFACE_CSV_FILE_NAME);
    if (!ifaceCsvFile) {
        NETMGR_LOG_E("ifstream failed");
        return false;
    }

    CSVRow row;
    std::lock_guard lock(mutex_);
    while (ifaceCsvFile >> row) {
        if (!UpdateIfaceStatsCsv(row[static_cast<uint32_t>(IfaceCsvColumn::IFACE_NAME)])) {
            NETMGR_LOG_E("UpdateIfaceStatsCsv failed, ifaceName[%{public}s]",
                row[static_cast<uint32_t>(IfaceCsvColumn::IFACE_NAME)].c_str());
            return false;
        }
    }
    return true;
}

bool NetStatsCsv::UpdateUidStats()
{
    std::ifstream uidCsvFile(CSV_DIR + UID_CSV_FILE_NAME);
    if (!uidCsvFile) {
        NETMGR_LOG_E("ifstream failed");
        return false;
    }

    // get current iface name, todo
    std::string iface = "eth0";
    CSVRow row;
    while (uidCsvFile >> row) {
        uint32_t uid = 0;
        if (!ParseCsvUint32(row[static_cast<uint32_t>(UidCsvColumn::UID)], uid)) {
            NETMGR_LOG_E("invalid uid in uid.csv");
            return false;
        }
        if (!UpdateUidStatsCsv(uid, iface)) {
            NETMGR_LOG_E("UpdateUidStatsCsv failed, uid[%{public}d], ifaceName[%{public}s]", uid, iface.c_str());
            return false;
        }
    }
    return true;
}

bool NetStatsCsv::UpdateIfaceStatsCsv(const std::string &iface)
{
    std::ofstream ifaceStatsCsvFile;
    ifaceStatsCsvFile.open(CSV_DIR + IFACE_STATS_CSV_FILE_NAME, std::fstream::app);
    if (!ifaceStatsCsvFile.is_open()) {
        NETMGR_LOG_E("ofstream open failed");
        ifaceStatsCsvFile.close();
        return false;
    }
    ifaceStatsCsvFile << iface << "," << GetCurrentTime() << ","
        << NetdController::GetInstance().GetIfaceRxBytes(iface) << "," <<
        NetdController::GetInstance().GetIfaceTxBytes(iface) << std::endl;

    ifaceStatsCsvFile.close();
    return true;
}

bool NetStatsCsv::UpdateUidStatsCsv(uint32_t uid, const std::string &iface)
{
    std::ofstream uidStatsCsvFile;
    uidStatsCsvFile.open(CSV_DIR + UID_STATS_CSV_FILE_NAME, std::fstream::app);
    if (!uidStatsCsvFile.is_open()) {
        NETMGR_LOG_E("ofstream open failed");
        uidStatsCsvFile.close();
        return false;
    }

    uidStatsCsvFile << uid<< "," << iface  << "," << GetCurrentTime() << ","
        << NetdController::GetInstance().GetUidOnIfaceRxBytes(uid, iface) << "," <<
        NetdController::GetInstance().GetUidOnIfaceTxBytes(uid, iface) << std::endl;
    uidStatsCsvFile.close();
    return true;
}

uint32_t NetStatsCsv::GetIfaceCalculateTime(const std::string &iface, uint32_t time)
{
    std::ifstream iface_file(CSV_DIR + IFACE_STATS_CSV_FILE_NAME, std::fstream::in);
    CSVRow row;
    uint32_t columnTime = 0;
    while (iface_file >> row) {
        if (iface.compare(row[static_cast<uint32_t>(IfaceStatsCsvColumn::IFACE_NAME)]) == 0) {
            if (!ParseCsvUint32(row[static_cast<uint32_t>(IfaceStatsCsvColumn::TIME)], columnTime)) {
                continue;
            }
            if (time <= columnTime) {
                return columnTime;
            }
        }
    }
    return columnTime;
}

uint32_t NetStatsCsv::GetUidCalculateTime(uint32_t uid, const std::string &iface, uint32_t time)
{
    std::ifstream uid_file(CSV_DIR + UID_STATS_CSV_FILE_NAME, std::fstream::in);
    CSVRow row;
    uint32_t columnTime = 0;
    while (uid_file >> row) {
        if ((std::to_string(uid).compare(row[static_cast<uint32_t>(UidStatCsvColumn::UID)]) == 0) &&
            (iface.compare(row[static_cast<uint32_t>(UidStatCsvColumn::IFACE_NAME)]) == 0)) {
            if (!ParseCsvUint32(row[static_cast<uint32_t>(UidStatCsvColumn::TIME)], columnTime)) {
                continue;
            }
            if (time <= columnTime) {
                return columnTime;
            }
        }
    }
    return columnTime;
}

void NetStatsCsv::GetSumStats(const std::vector<NetStatsInfo> &vecRow, NetStatsInfo &sumStats)
{
    uint32_t restartTimes = 0;
    NetStatsInfo startStats = vecRow[0];
    for (uint32_t i = 1; i < vecRow.size(); i++) {
        if (vecRow[i].rxBytes_ < vecRow[i - 1].rxBytes_) {
            restartTimes++;
            NetStatsInfo endStats = vecRow[i - 1];
            GetPeriodStats(startStats, endStats, sumStats);
            startStats = vecRow[i];
        }
    }
    if (restartTimes >= 1) {
        GetPeriodStats(startStats, vecRow[vecRow.size() - 1], sumStats);
    }
    if (restartTimes == 0) {
        GetPeriodStats(vecRow[0], vecRow[vecRow.size() - 1], sumStats);
    }
}

void NetStatsCsv::GetPeriodStats(const NetStatsInfo &startStats, const NetStatsInfo &endStats, NetStatsInfo &totalStats)
{
    totalStats.rxBytes_ += endStats.rxBytes_ - startStats.rxBytes_;
    totalStats.txBytes_ += endStats.txBytes_ - startStats.txBytes_;
}

void NetStatsCsv::GetCalculateStatsInfo(const CSVRow &row, uint32_t calStartTime, uint32_t calEndTime,
    uint32_t timeCloumn, std::vector<NetStatsInfo> &vecRow)
{
    uint32_t rowTime = 0;
    if (!ParseCsvUint32(row[(timeCloumn)], rowTime)) {
        return;
    }
    if (calStartTime <= rowTime && rowTime  <= calEndTime) {
        uint32_t rxColumn = static_cast<uint32_t>(++timeCloumn);
        uint32_t txCloumn = static_cast<uint32_t>(++timeCloumn);
        NetStatsInfo statsRow;
        if (!ParseCsvInt64(row[rxColumn], statsRow.rxBytes_) ||
            !ParseCsvInt64(row[txCloumn], statsRow.txBytes_)) {
            return;
        }
        vecRow.push_back(statsRow);
    }
}

NetStatsResultCode NetStatsCsv::GetIfaceBytes(const std::string &iface, uint32_t start, uint32_t end,
    NetStatsInfo &statsInfo)
{
    uint32_t calStartTime = GetIfaceCalculateTime(iface, start);
    uint32_t calEndTime = GetIfaceCalculateTime(iface, end);
    if (calStartTime == calEndTime) {
        statsInfo.rxBytes_ = 0;
        statsInfo.txBytes_ = 0;
        NETMGR_LOG_E("start and end less equal Minimum time or greater equal Maximum time");
        return NetStatsResultCode::ERR_INTERNAL_ERROR;
    }

    CSVRow row;
    std::vector<NetStatsInfo> vecRow;
    std::ifstream iface_file(CSV_DIR + IFACE_STATS_CSV_FILE_NAME, std::fstream::in);
    while (iface_file >> row) {
        if (iface.compare(row[static_cast<uint32_t>(IfaceStatsCsvColumn::IFACE_NAME)]) == 0) {
            uint32_t timeCloumn = static_cast<uint32_t>(IfaceStatsCsvColumn::TIME);
            GetCalculateStatsInfo(row, calStartTime, calEndTime, timeCloumn, vecRow);
        }
    }
    GetSumStats(vecRow, statsInfo);
    NETMGR_LOG_I("GetIfaceBytes statsInfo.rxBytes[%{public}" PRId64 "]"
        "statsInfo.txBytes[%{public}" PRId64 "]", statsInfo.rxBytes_, statsInfo.txBytes_);
    return NetStatsResultCode::ERR_NONE;
}

NetStatsResultCode NetStatsCsv::GetUidBytes(const std::string &iface, uint32_t uid, uint32_t start,
    uint32_t end, NetStatsInfo &statsInfo)
{
    uint32_t calStartTime = GetUidCalculateTime(uid, iface, start);
    uint32_t calEndTime = GetUidCalculateTime(uid, iface, end);
    if (calStartTime == calEndTime) {
        statsInfo.rxBytes_ = 0;
        statsInfo.txBytes_ = 0;
        NETMGR_LOG_E("start and end less equal Minimum time or greater equal Maximum time");
        return NetStatsResultCode::ERR_INTERNAL_ERROR;
    }

    std::ifstream uid_file(CSV_DIR + UID_STATS_CSV_FILE_NAME, std::fstream::in);
    CSVRow row;
    std::vector<NetStatsInfo> vecRow;
    while (uid_file >> row) {
        if ((std::to_string(uid).compare(row[static_cast<uint32_t>(UidStatCsvColumn::UID)]) == 0) &&
            (iface.compare(row[static_cast<uint32_t>(UidStatCsvColumn::IFACE_NAME)]) == 0)) {
            uint32_t timeCloumn = static_cast<uint32_t>(UidStatCsvColumn::TIME);
            GetCalculateStatsInfo(row, calStartTime, calEndTime, timeCloumn, vecRow);
        }
    }
    GetSumStats(vecRow, statsInfo);
    NETMGR_LOG_I("GetUidBytes statsInfo.rxBytes[%{public}" PRId64 "]"
        "statsInfo.txBytes[%{public}" PRId64 "]", statsInfo.rxBytes_, statsInfo.txBytes_);
    return NetStatsResultCode::ERR_NONE;
}

std::string NetStatsCsv::GetCurrentTime()
{
    time_t now;
    time(&now);
    std::stringstream ss;
    ss << now;
    return ss.str();
}

NetStatsResultCode NetStatsCsv::ResetFactory()
{
    if (remove((CSV_DIR + IFACE_CSV_FILE_NAME).c_str()) == -1 ||
        remove((CSV_DIR + UID_CSV_FILE_NAME).c_str()) == -1 ||
        remove((CSV_DIR + IFACE_STATS_CSV_FILE_NAME).c_str()) == -1 ||
        remove((CSV_DIR + UID_STATS_CSV_FILE_NAME).c_str()) == -1) {
        NETMGR_LOG_I("ResetFactory is failed");
        return NetStatsResultCode::ERR_INTERNAL_ERROR;
    }
    return NetStatsResultCode::ERR_NONE;
}
} // namespace NetManagerStandard
} // namespace OHOS

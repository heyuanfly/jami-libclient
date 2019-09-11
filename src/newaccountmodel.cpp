/****************************************************************************
 *    Copyright (C) 2017-2019 Savoir-faire Linux Inc.                       *
 *   Author: Nicolas Jäger <nicolas.jager@savoirfairelinux.com>             *
 *   Author: Sébastien Blin <sebastien.blin@savoirfairelinux.com>           *
 *   Author: Kateryna Kostiuk <kateryna.kostiuk@savoirfairelinux.com>       *
 *   Author: Andreas Traczyk <andreas.traczyk@savoirfairelinux.com>         *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Lesser General Public             *
 *   License as published by the Free Software Foundation; either           *
 *   version 2.1 of the License, or (at your option) any later version.     *
 *                                                                          *
 *   This library is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/
#include "api/newaccountmodel.h"

// daemon
#include <account_const.h>

//qt
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include <QtCore/QBuffer>

// new LRC
#include "api/lrc.h"
#include "api/contactmodel.h"
#include "api/conversationmodel.h"
#include "api/peerdiscoverymodel.h"
#include "api/newcallmodel.h"
#include "api/newcodecmodel.h"
#include "api/newdevicemodel.h"
#include "api/behaviorcontroller.h"
#include "authority/storagehelper.h"
#include "callbackshandler.h"
#include "database.h"
#include "vcard.h"

// old LRC
#include "api/profile.h"
#include "qtwrapper/conversions_wrap.hpp"

// Dbus
#include "dbus/configurationmanager.h"

#include <atomic>

namespace lrc
{

using namespace api;

class NewAccountModelPimpl: public QObject
{
    Q_OBJECT
public:
    NewAccountModelPimpl(NewAccountModel& linked,
                         Lrc& lrc,
                         const CallbacksHandler& callbackHandler,
                         const BehaviorController& behaviorController,
                         MigrationCb& willMigrateCb,
                         MigrationCb& didMigrateCb);
    ~NewAccountModelPimpl();

    using AccountInfoDbMap = std::map<std::string,
                                      std::pair<account::Info, std::shared_ptr<Database>>>;

    NewAccountModel& linked;
    Lrc& lrc;
    const CallbacksHandler& callbacksHandler;
    const BehaviorController& behaviorController;
    AccountInfoDbMap accounts;

    // Synchronization tools
    std::mutex m_mutex_account;
    std::mutex m_mutex_account_removal;
    std::condition_variable m_condVar_account_removal;
    std::atomic_bool username_changed;
    std::string new_username;

    /**
     * Add the profile information from an account to the db then add it to accounts.
     * @param accountId
     * @param db an optional migrated database object
     * @note this method get details for an account from the daemon.
     */
    void addToAccounts(const std::string& accountId, std::shared_ptr<Database> db = nullptr);

    /**
     * Remove account from accounts list. Emit accountRemoved.
     * @param accountId
     */
    void removeFromAccounts(const std::string& accountId);

    /**
     * Sync changes to the accounts list with the lrc.
     */
    void updateAccounts();

public Q_SLOTS:

    /**
     * Emit accountStatusChanged.
     * @param accountId
     * @param status
     */
    void slotAccountStatusChanged(const std::string& accountID, const api::account::Status status);

    /**
     * Emit exportOnRingEnded.
     * @param accountId
     * @param status
     * @param pin
     */
    void slotExportOnRingEnded(const std::string& accountID, int status, const std::string& pin);

    /**
     * @param accountId
     * @param details
     */
    void slotAccountDetailsChanged(const std::string& accountID, const std::map<std::string, std::string>& details);

    /**
     * Emit nameRegistrationEnded
     * @param accountId
     * @param status
     * @param name
     */
    void slotNameRegistrationEnded(const std::string& accountId, int status, const std::string& name);

    /**
     * Emit registeredNameFound
     * @param accountId
     * @param status
     * @param address
     * @param name
     */
    void slotRegisteredNameFound(const std::string& accountId, int status, const std::string& address, const std::string& name);

    /**
     * Emit migrationEnded
     * @param accountId
     * @param ok
     */
    void slotMigrationEnded(const std::string& accountId, bool ok);
};

NewAccountModel::NewAccountModel(Lrc& lrc,
                                 const CallbacksHandler& callbacksHandler,
                                 const BehaviorController& behaviorController,
                                 MigrationCb& willMigrateCb,
                                 MigrationCb& didMigrateCb)
: QObject()
, pimpl_(std::make_unique<NewAccountModelPimpl>(*this, lrc, callbacksHandler, behaviorController,
                                                willMigrateCb, didMigrateCb))
{
}

NewAccountModel::~NewAccountModel()
{
}

std::vector<std::string>
NewAccountModel::getAccountList() const
{
    std::vector<std::string> accountsId;
    const QStringList accountIds = ConfigurationManager::instance().getAccountList();

    for (auto const& id : accountIds) {
        auto account = pimpl_->accounts.find(id.toStdString());
        // Do not include accounts flagged for removal
        if (account != pimpl_->accounts.end() && account->second.first.valid)
            accountsId.emplace_back(id.toStdString());
    }

    return accountsId;
}

void
NewAccountModel::setAccountEnabled(const std::string& accountId, bool enabled) const
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end()) {
        throw std::out_of_range("NewAccountModel::getAccountConfig, can't find " + accountId);
    }
    auto& accountInfo = account->second.first;
    accountInfo.enabled = enabled;
    ConfigurationManager::instance().sendRegister(QString::fromStdString(accountId), enabled);
}

void
NewAccountModel::setAccountConfig(const std::string& accountId,
                                  const account::ConfProperties_t& confProperties) const
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end()) {
        throw std::out_of_range("NewAccountModel::save, can't find " + accountId);
    }
    auto& accountInfo = account->second.first;
    auto& configurationManager = ConfigurationManager::instance();
    MapStringString details = confProperties.toDetails();
    // Set values from Info. No need to include ID and TYPE. SIP accounts may modify the USERNAME
    // TODO: move these into the ConfProperties_t struct ?
    using namespace DRing::Account;
    qDebug("UPNP_ENABLED: %s\n", details[ConfProperties::UPNP_ENABLED].toStdString().c_str());
    details[ConfProperties::ENABLED]                    = toQString(accountInfo.enabled);
    details[ConfProperties::ALIAS]                      = toQString(accountInfo.profileInfo.alias);
    details[ConfProperties::DISPLAYNAME]                = toQString(accountInfo.profileInfo.alias);
    details[ConfProperties::TYPE]                       = (accountInfo.profileInfo.type == profile::Type::RING) ? QString(ProtocolNames::RING) : QString(ProtocolNames::SIP);
    if (accountInfo.profileInfo.type == profile::Type::RING) {
        details[ConfProperties::USERNAME] = toQString(accountInfo.profileInfo.uri).prepend((accountInfo.profileInfo.type == profile::Type::RING) ? "ring:" : "");
    } else if (accountInfo.profileInfo.type == profile::Type::SIP) {
        MapStringString credentials;
        credentials[ConfProperties::USERNAME] = toQString(confProperties.username);
        credentials[ConfProperties::PASSWORD] = toQString(confProperties.password);
        credentials[ConfProperties::REALM] = confProperties.realm.empty()? QString("*") : toQString(confProperties.realm);
        QVector<MapStringString> credentialsVec;
        credentialsVec.append(credentials);
        ConfigurationManager::instance().setCredentials(accountId.c_str(), credentialsVec);
        details[ConfProperties::USERNAME] = toQString(confProperties.username);
    }
    configurationManager.setAccountDetails(QString::fromStdString(accountId), details);
}

account::ConfProperties_t
NewAccountModel::getAccountConfig(const std::string& accountId) const
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end()) {
        throw std::out_of_range("NewAccountModel::getAccountConfig, can't find " + accountId);
    }
    auto& accountInfo = account->second.first;
    return accountInfo.confProperties;
}

void
NewAccountModel::setAlias(const std::string& accountId, const std::string& alias)
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end()) {
        throw std::out_of_range("NewAccountModel::setAlias, can't find " + accountId);
    }
    auto& accountInfo = account->second.first;
    accountInfo.profileInfo.alias = alias;

    authority::storage::createOrUpdateProfile(accountInfo.id, accountInfo.profileInfo);

    emit profileUpdated(accountId);
}

void
NewAccountModel::setAvatar(const std::string& accountId, const std::string& avatar)
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end()) {
        throw std::out_of_range("NewAccountModel::setAvatar, can't find " + accountId);
    }
    auto& accountInfo = account->second.first;
    accountInfo.profileInfo.avatar = avatar;

    authority::storage::createOrUpdateProfile(accountInfo.id, accountInfo.profileInfo);

    emit profileUpdated(accountId);
}

bool
NewAccountModel::registerName(const std::string& accountId, const std::string& password, const std::string& username)
{
    return ConfigurationManager::instance().registerName(accountId.c_str(), password.c_str(), username.c_str());
}

bool
NewAccountModel::exportToFile(const std::string& accountId, const std::string& path, const std::string& password) const
{
    return ConfigurationManager::instance().exportToFile(accountId.c_str(), path.c_str(), password.c_str());
}

bool
NewAccountModel::exportOnRing(const std::string& accountId, const std::string& password) const
{
    return ConfigurationManager::instance().exportOnRing(accountId.c_str(), password.c_str());
}

void
NewAccountModel::removeAccount(const std::string& accountId) const
{
    ConfigurationManager::instance().removeAccount(accountId.c_str());
}

bool
NewAccountModel::changeAccountPassword(const std::string& accountId,
                                       const std::string& currentPassword,
                                       const std::string& newPassword) const
{
    return ConfigurationManager::instance()
    .changeAccountPassword(accountId.c_str(), currentPassword.c_str(), newPassword.c_str());
}

void
NewAccountModel::flagFreeable(const std::string& accountId) const
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end())
        throw std::out_of_range("NewAccountModel::flagFreeable, can't find " + accountId);

    {
        std::lock_guard<std::mutex> lock(pimpl_->m_mutex_account_removal);
        account->second.first.freeable = true;
    }
    pimpl_->m_condVar_account_removal.notify_all();
}

const account::Info&
NewAccountModel::getAccountInfo(const std::string& accountId) const
{
    auto accountInfo = pimpl_->accounts.find(accountId);
    if (accountInfo == pimpl_->accounts.end())
        throw std::out_of_range("NewAccountModel::getAccountInfo, can't find " + accountId);

    return accountInfo->second.first;
}

NewAccountModelPimpl::NewAccountModelPimpl(NewAccountModel& linked,
                                           Lrc& lrc,
                                           const CallbacksHandler& callbacksHandler,
                                           const BehaviorController& behaviorController,
                                           MigrationCb& willMigrateCb,
                                           MigrationCb& didMigrateCb)
: linked(linked)
, lrc {lrc}
, behaviorController(behaviorController)
, callbacksHandler(callbacksHandler)
, username_changed(false)
{
    const QStringList accountIds = ConfigurationManager::instance().getAccountList();
    auto accountDbs = authority::storage::migrateIfNeeded(accountIds, willMigrateCb, didMigrateCb);
    for (const auto& id : accountIds) {
        addToAccounts(id.toStdString(), accountDbs.at(accountIds.indexOf(id)));
    }

    connect(&callbacksHandler, &CallbacksHandler::accountsChanged, this, &NewAccountModelPimpl::updateAccounts);
    connect(&callbacksHandler, &CallbacksHandler::accountStatusChanged, this, &NewAccountModelPimpl::slotAccountStatusChanged);
    connect(&callbacksHandler, &CallbacksHandler::accountDetailsChanged, this, &NewAccountModelPimpl::slotAccountDetailsChanged);
    connect(&callbacksHandler, &CallbacksHandler::exportOnRingEnded, this, &NewAccountModelPimpl::slotExportOnRingEnded);
    connect(&callbacksHandler, &CallbacksHandler::nameRegistrationEnded, this, &NewAccountModelPimpl::slotNameRegistrationEnded);
    connect(&callbacksHandler, &CallbacksHandler::registeredNameFound, this, &NewAccountModelPimpl::slotRegisteredNameFound);
    connect(&callbacksHandler, &CallbacksHandler::migrationEnded, this, &NewAccountModelPimpl::slotMigrationEnded);
}

NewAccountModelPimpl::~NewAccountModelPimpl()
{
}

void
NewAccountModelPimpl::updateAccounts()
{
    qDebug() << "Syncing lrc accounts list with the daemon";
    ConfigurationManagerInterface& configurationManager = ConfigurationManager::instance();
    QStringList accountIds = configurationManager.getAccountList();

    // Detect removed accounts
    std::list<std::string> toBeRemoved;
    for (auto& it : accounts) {
        auto& accountInfo = it.second.first;
        if (!accountIds.contains(QString::fromStdString(accountInfo.id))) {
            qDebug("detected account removal %s", accountInfo.id.c_str());
            toBeRemoved.push_back(accountInfo.id);
        }
    }

    for (auto it = toBeRemoved.begin(); it != toBeRemoved.end(); ++it) {
        removeFromAccounts(*it);
    }

    // Detect new accounts
    for (auto& id : accountIds) {
        auto account = accounts.find(id.toStdString());
        if (account == accounts.end()) {
            qDebug("detected new account %s", id.toStdString().c_str());
            addToAccounts(id.toStdString());
            auto updatedAccount = accounts.find(id.toStdString());
            if (updatedAccount == accounts.end()) {
                return;
            }
            if (updatedAccount->second.first.profileInfo.type == profile::Type::SIP) {
                // NOTE: At this point, a SIP account is ready, but not a Ring
                // account. Indeed, the keys are not generated at this point.
                // See slotAccountStatusChanged for more details.
                emit linked.accountAdded(id.toStdString());
            }
        }
    }
}

void
NewAccountModelPimpl::slotAccountStatusChanged(const std::string& accountID, const api::account::Status status)
{
    if (status == api::account::Status::INVALID) {
        emit linked.invalidAccountDetected(accountID);
        return;
    }
    auto it = accounts.find(accountID);

    // If account is not in the map yet, don't add it, it is updateAccounts's job
    if (it == accounts.end()) {
        return;
    }

    auto& accountInfo = it->second.first;

    if (accountInfo.profileInfo.type != profile::Type::SIP) {
        if (status != api::account::Status::INITIALIZING
            && accountInfo.status == api::account::Status::INITIALIZING) {
            // Detect when a new account is generated (keys are ready). During
            // the generation, a Ring account got the "INITIALIZING" status.
            // When keys are generated, the status will change.
            accounts.erase(accountID);
            addToAccounts(accountID);
            emit linked.accountAdded(accountID);
        } else if (!accountInfo.profileInfo.uri.empty()) {
            accountInfo.status = status;
            emit linked.accountStatusChanged(accountID);
        }
    } else {
        accountInfo.status = status;
        emit linked.accountStatusChanged(accountID);
    }
}

void
NewAccountModelPimpl::slotAccountDetailsChanged(const std::string& accountId, const std::map<std::string, std::string>& details)
{
    auto account = accounts.find(accountId);
    if (account == accounts.end()) {
        throw std::out_of_range("NewAccountModelPimpl::slotAccountDetailsChanged, can't find " + accountId);
    }
    auto& accountInfo = account->second.first;
    accountInfo.fromDetails(convertMap(details));
    if (username_changed) {
        username_changed = false;
        accountInfo.registeredName = new_username;
        emit linked.profileUpdated(accountId);
    }
    emit linked.accountStatusChanged(accountId);
}

void
NewAccountModelPimpl::slotExportOnRingEnded(const std::string& accountID, int status, const std::string& pin)
{
    account::ExportOnRingStatus convertedStatus = account::ExportOnRingStatus::INVALID;
    switch (status) {
    case 0:
        convertedStatus = account::ExportOnRingStatus::SUCCESS;
        break;
    case 1:
        convertedStatus = account::ExportOnRingStatus::WRONG_PASSWORD;
        break;
    case 2:
        convertedStatus = account::ExportOnRingStatus::NETWORK_ERROR;
        break;
    default:
        break;
    }
    emit linked.exportOnRingEnded(accountID, convertedStatus, pin);
}

void
NewAccountModelPimpl::slotNameRegistrationEnded(const std::string& accountId, int status, const std::string& name)
{
    account::RegisterNameStatus convertedStatus = account::RegisterNameStatus::INVALID;
    switch (status)
    {
    case 0: {
        convertedStatus = account::RegisterNameStatus::SUCCESS;
        auto account = accounts.find(accountId);
        if (account != accounts.end() && account->second.first.registeredName.empty()) {
            auto conf = linked.getAccountConfig(accountId);
            username_changed = true;
            new_username = name;
            linked.setAccountConfig(accountId, conf);
        }
        break;
      }
    case 1:
        convertedStatus = account::RegisterNameStatus::WRONG_PASSWORD;
        break;
    case 2:
        convertedStatus = account::RegisterNameStatus::INVALID_NAME;
        break;
    case 3:
        convertedStatus = account::RegisterNameStatus::ALREADY_TAKEN;
        break;
    case 4:
        convertedStatus = account::RegisterNameStatus::NETWORK_ERROR;
        break;
    default:
        break;
    }
    emit linked.nameRegistrationEnded(accountId, convertedStatus, name);
}

void
NewAccountModelPimpl::slotRegisteredNameFound(const std::string& accountId, int status, const std::string& address, const std::string& name)
{
    account::LookupStatus convertedStatus = account::LookupStatus::INVALID;
    switch (status)
    {
    case 0:
        convertedStatus = account::LookupStatus::SUCCESS;
        break;
    case 1:
        convertedStatus = account::LookupStatus::INVALID_NAME;
        break;
    case 2:
        convertedStatus = account::LookupStatus::NOT_FOUND;
        break;
    case 3:
        convertedStatus = account::LookupStatus::ERROR;
        break;
    default:
        break;
    }
    emit linked.registeredNameFound(accountId, convertedStatus, address, name);
}

void
NewAccountModelPimpl::slotMigrationEnded(const std::string& accountId, bool ok)
{
    if (ok) {
        addToAccounts(accountId);
    }
    emit linked.migrationEnded(accountId, ok);
}

void
NewAccountModelPimpl::addToAccounts(const std::string& accountId,
                                    std::shared_ptr<Database> db)
{
    if (db == nullptr) {
        try {
            auto appPath = authority::storage::getPath();
            auto dbName = QString::fromStdString(accountId + "/history");
            db = DatabaseFactory::create<Database>(dbName, appPath);
            // create the profiles path if necessary
            QDir profilesDir(appPath + QString::fromStdString(accountId) + "/profiles");
            if (!profilesDir.exists()) {
                profilesDir.mkpath(".");
            }
        } catch (const std::runtime_error& e) {
            qWarning() << e.what();
            return;
        }
    }

    auto it = accounts.emplace(accountId, std::make_pair(account::Info(), db));

    if (!it.second) {
        qDebug("failed to add new account: id already present in map");
        return;
    }

    // Init profile
    account::Info& newAccInfo = (it.first)->second.first;
    newAccInfo.id = accountId;
    newAccInfo.profileInfo.avatar = authority::storage::getAccountAvatar(accountId);

    // Fill account::Info struct with details from daemon
    MapStringString details = ConfigurationManager::instance().getAccountDetails(accountId.c_str());
    newAccInfo.fromDetails(details);

    // Init models for this account
    newAccInfo.accountModel = &linked;
    newAccInfo.callModel = std::make_unique<NewCallModel>(newAccInfo, callbacksHandler);
    newAccInfo.contactModel = std::make_unique<ContactModel>(newAccInfo, *db, callbacksHandler, behaviorController);
    newAccInfo.conversationModel = std::make_unique<ConversationModel>(newAccInfo, lrc, *db, callbacksHandler, behaviorController);
    newAccInfo.peerDiscoveryModel = std::make_unique<PeerDiscoveryModel>(callbacksHandler, accountId);
    newAccInfo.deviceModel = std::make_unique<NewDeviceModel>(newAccInfo, callbacksHandler);
    newAccInfo.codecModel = std::make_unique<NewCodecModel>(newAccInfo, callbacksHandler);

    MapStringString volatileDetails = ConfigurationManager::instance().getVolatileAccountDetails(accountId.c_str());
    std::string daemonStatus = volatileDetails[DRing::Account::ConfProperties::Registration::STATUS].toStdString();
    newAccInfo.status = lrc::api::account::to_status(daemonStatus);
}

void
NewAccountModelPimpl::removeFromAccounts(const std::string& accountId)
{
    /* Update db before waiting for the client to stop using the structs is fine
       as long as we don't free anything */
    auto account = accounts.find(accountId);
    if (account == accounts.end()) {
        return;
    }
    auto& accountInfo = account->second.first;
    auto& accountDb = *(account->second.second);
    accountDb.remove();
    authority::storage::removeAccount(accountId);

    /* Inform client about account removal. Do *not* free account structures
       before we are sure that the client stopped using it, otherwise we might
       get into use-after-free troubles. */
    accountInfo.valid = false;
    emit linked.accountRemoved(accountId);

#ifdef CHK_FREEABLE_BEFORE_ERASE_ACCOUNT
    std::unique_lock<std::mutex> lock(m_mutex_account_removal);
    // Wait for client to stop using old account structs
    m_condVar_account_removal.wait(lock, [&](){return accounts[accountId].freeable;});
    lock.unlock();
#endif

    // Now we can free them
    accounts.erase(accountId);
}

void
account::Info::fromDetails(const MapStringString& details)
{
    using namespace DRing::Account;
    const MapStringString volatileDetails = ConfigurationManager::instance().getVolatileAccountDetails(id.c_str());

    // General
    if (details[ConfProperties::TYPE] != "")
        profileInfo.type                                = details[ConfProperties::TYPE] == QString(ProtocolNames::RING) ? profile::Type::RING : profile::Type::SIP;
    registeredName                                      = profileInfo.type == profile::Type::RING ? volatileDetails[VolatileProperties::REGISTERED_NAME].toStdString() : "";
    profileInfo.alias                                   = toStdString(details[ConfProperties::DISPLAYNAME]);
    enabled                                             = toBool(details[ConfProperties::ENABLED]);
    confProperties.mailbox                              = toStdString(details[ConfProperties::MAILBOX]);
    confProperties.dtmfType                             = toStdString(details[ConfProperties::DTMF_TYPE]);
    confProperties.autoAnswer                           = toBool(details[ConfProperties::AUTOANSWER]);
    confProperties.activeCallLimit                      = toInt(details[ConfProperties::ACTIVE_CALL_LIMIT]);
    confProperties.hostname                             = toStdString(details[ConfProperties::HOSTNAME]);
    profileInfo.uri                                     = (profileInfo.type == profile::Type::RING and details[ConfProperties::USERNAME].contains("ring:"))
                                                          ? details[ConfProperties::USERNAME].toStdString().substr(std::string("ring:").size())
                                                          : details[ConfProperties::USERNAME].toStdString();
    confProperties.username                             = toStdString(details[ConfProperties::USERNAME]);
    confProperties.routeset                             = toStdString(details[ConfProperties::ROUTE]);
    confProperties.password                             = toStdString(details[ConfProperties::PASSWORD]);
    confProperties.realm                                = toStdString(details[ConfProperties::REALM]);
    confProperties.localInterface                       = toStdString(details[ConfProperties::LOCAL_INTERFACE]);
    confProperties.deviceId                             = toStdString(details[ConfProperties::RING_DEVICE_ID]);
    confProperties.deviceName                           = toStdString(details[ConfProperties::RING_DEVICE_NAME]);
    confProperties.publishedSameAsLocal                 = toBool(details[ConfProperties::PUBLISHED_SAMEAS_LOCAL]);
    confProperties.localPort                            = toInt(details[ConfProperties::LOCAL_PORT]);
    confProperties.publishedPort                        = toInt(details[ConfProperties::PUBLISHED_PORT]);
    confProperties.publishedAddress                     = toStdString(details[ConfProperties::PUBLISHED_ADDRESS]);
    confProperties.userAgent                            = toStdString(details[ConfProperties::USER_AGENT]);
    confProperties.upnpEnabled                          = toBool(details[ConfProperties::UPNP_ENABLED]);
    confProperties.hasCustomUserAgent                   = toBool(details[ConfProperties::HAS_CUSTOM_USER_AGENT]);
    confProperties.allowIncoming                        = toBool(details[ConfProperties::ALLOW_CERT_FROM_HISTORY])
                                                        | toBool(details[ConfProperties::ALLOW_CERT_FROM_CONTACT])
                                                        | toBool(details[ConfProperties::ALLOW_CERT_FROM_TRUSTED]);
    confProperties.archivePassword                      = toStdString(details[ConfProperties::ARCHIVE_PASSWORD]);
    confProperties.archiveHasPassword                   = toBool(details[ConfProperties::ARCHIVE_HAS_PASSWORD]);
    confProperties.archivePath                          = toStdString(details[ConfProperties::ARCHIVE_PATH]);
    confProperties.archivePin                           = toStdString(details[ConfProperties::ARCHIVE_PIN]);
    confProperties.proxyEnabled                         = toBool(details[ConfProperties::PROXY_ENABLED]);
    confProperties.proxyServer                          = toStdString(details[ConfProperties::PROXY_SERVER]);
    confProperties.proxyPushToken                       = toStdString(details[ConfProperties::PROXY_PUSH_TOKEN]);
    confProperties.peerDiscovery                        = toBool(details[ConfProperties::DHT_PEER_DISCOVERY]);
    confProperties.accountDiscovery                     = toBool(details[ConfProperties::ACCOUNT_PEER_DISCOVERY]);
    confProperties.accountPublish                       = toBool(details[ConfProperties::ACCOUNT_PUBLISH]);
    // Audio
    confProperties.Audio.audioPortMax                   = toInt(details[ConfProperties::Audio::PORT_MAX]);
    confProperties.Audio.audioPortMin                   = toInt(details[ConfProperties::Audio::PORT_MIN]);
    // Video
    confProperties.Video.videoEnabled                   = toBool(details[ConfProperties::Video::ENABLED]);
    confProperties.Video.videoPortMax                   = toInt(details[ConfProperties::Video::PORT_MAX]);
    confProperties.Video.videoPortMin                   = toInt(details[ConfProperties::Video::PORT_MIN]);
    // STUN
    confProperties.STUN.server                          = toStdString(details[ConfProperties::STUN::SERVER]);
    confProperties.STUN.enable                          = toBool(details[ConfProperties::STUN::ENABLED]);
    // TURN
    confProperties.TURN.server                          = toStdString(details[ConfProperties::TURN::SERVER]);
    confProperties.TURN.enable                          = toBool(details[ConfProperties::TURN::ENABLED]);
    confProperties.TURN.username                        = toStdString(details[ConfProperties::TURN::SERVER_UNAME]);
    confProperties.TURN.password                        = toStdString(details[ConfProperties::TURN::SERVER_PWD]);
    confProperties.TURN.realm                           = toStdString(details[ConfProperties::TURN::SERVER_REALM]);
    // Presence
    confProperties.Presence.presencePublishSupported    = toBool(details[ConfProperties::Presence::SUPPORT_PUBLISH]);
    confProperties.Presence.presenceSubscribeSupported  = toBool(details[ConfProperties::Presence::SUPPORT_SUBSCRIBE]);
    confProperties.Presence.presenceEnabled             = toBool(details[ConfProperties::Presence::ENABLED]);
    // Ringtone
    confProperties.Ringtone.ringtonePath                = toStdString(details[ConfProperties::Ringtone::PATH]);
    confProperties.Ringtone.ringtoneEnabled             = toBool(details[ConfProperties::Ringtone::ENABLED]);
    // SRTP
    confProperties.SRTP.keyExchange                     = toStdString(details[ConfProperties::SRTP::KEY_EXCHANGE]).empty()? account::KeyExchangeProtocol::NONE : account::KeyExchangeProtocol::SDES;
    confProperties.SRTP.enable                          = toBool(details[ConfProperties::SRTP::ENABLED]);
    confProperties.SRTP.rtpFallback                     = toBool(details[ConfProperties::SRTP::RTP_FALLBACK]);
    // TLS
    confProperties.TLS.listenerPort                     = toInt(details[ConfProperties::TLS::LISTENER_PORT]);
    confProperties.TLS.enable                           = details[ConfProperties::TYPE] == QString(ProtocolNames::RING)? true : toBool(details[ConfProperties::TLS::ENABLED]);
    confProperties.TLS.port                             = toInt(details[ConfProperties::TLS::PORT]);
    confProperties.TLS.certificateListFile              = toStdString(details[ConfProperties::TLS::CA_LIST_FILE]);
    confProperties.TLS.certificateFile                  = toStdString(details[ConfProperties::TLS::CERTIFICATE_FILE]);
    confProperties.TLS.privateKeyFile                   = toStdString(details[ConfProperties::TLS::PRIVATE_KEY_FILE]);
    confProperties.TLS.password                         = toStdString(details[ConfProperties::TLS::PASSWORD]);
    auto method = toStdString(details[ConfProperties::TLS::METHOD]);
    if (method == "TLSv1") {
        confProperties.TLS.method                       = account::TlsMethod::TLSv1;
    } else if (method == "TLSv1.1") {
        confProperties.TLS.method                       = account::TlsMethod::TLSv1_1;
    } else if (method == "TLSv1.2") {
        confProperties.TLS.method                       = account::TlsMethod::TLSv1_2;
    } else {
        confProperties.TLS.method                       = account::TlsMethod::DEFAULT;
    }
    confProperties.TLS.ciphers                          = toStdString(details[ConfProperties::TLS::CIPHERS]);
    confProperties.TLS.serverName                       = toStdString(details[ConfProperties::TLS::SERVER_NAME]);
    confProperties.TLS.verifyServer                     = toBool(details[ConfProperties::TLS::VERIFY_SERVER]);
    confProperties.TLS.verifyClient                     = toBool(details[ConfProperties::TLS::VERIFY_CLIENT]);
    confProperties.TLS.requireClientCertificate         = toBool(details[ConfProperties::TLS::REQUIRE_CLIENT_CERTIFICATE]);
    confProperties.TLS.negotiationTimeoutSec            = toInt(details[ConfProperties::TLS::NEGOTIATION_TIMEOUT_SEC]);
    // DHT
    confProperties.DHT.port                             = toInt(details[ConfProperties::DHT::PORT]);
    confProperties.DHT.PublicInCalls                    = toBool(details[ConfProperties::DHT::PUBLIC_IN_CALLS]);
    confProperties.DHT.AllowFromTrusted                 = toBool(details[ConfProperties::DHT::ALLOW_FROM_TRUSTED]);
    // RingNS
    confProperties.RingNS.uri                           = toStdString(details[ConfProperties::RingNS::URI]);
    confProperties.RingNS.account                       = toStdString(details[ConfProperties::RingNS::ACCOUNT]);
    // Registration
    confProperties.Registration.expire                  = toInt(details[ConfProperties::Registration::EXPIRE]);
}

MapStringString
account::ConfProperties_t::toDetails() const
{
    using namespace DRing::Account;
    MapStringString details;
    // General
    details[ConfProperties::MAILBOX]                    = toQString(this->mailbox);
    details[ConfProperties::DTMF_TYPE]                  = toQString(this->dtmfType);
    details[ConfProperties::AUTOANSWER]                 = toQString(this->autoAnswer);
    details[ConfProperties::ACTIVE_CALL_LIMIT]          = toQString(this->activeCallLimit);
    details[ConfProperties::HOSTNAME]                   = toQString(this->hostname);
    details[ConfProperties::ROUTE]                      = toQString(this->routeset);
    details[ConfProperties::PASSWORD]                   = toQString(this->password);
    details[ConfProperties::REALM]                      = toQString(this->realm);
    details[ConfProperties::RING_DEVICE_ID]             = toQString(this->deviceId);
    details[ConfProperties::RING_DEVICE_NAME]           = toQString(this->deviceName);
    details[ConfProperties::LOCAL_INTERFACE]            = toQString(this->localInterface);
    details[ConfProperties::PUBLISHED_SAMEAS_LOCAL]     = toQString(this->publishedSameAsLocal);
    details[ConfProperties::LOCAL_PORT]                 = toQString(this->localPort);
    details[ConfProperties::PUBLISHED_PORT]             = toQString(this->publishedPort);
    details[ConfProperties::PUBLISHED_ADDRESS]          = toQString(this->publishedAddress);
    details[ConfProperties::USER_AGENT]                 = toQString(this->userAgent);
    details[ConfProperties::UPNP_ENABLED]               = toQString(this->upnpEnabled);
    details[ConfProperties::HAS_CUSTOM_USER_AGENT]      = toQString(this->hasCustomUserAgent);
    details[ConfProperties::ALLOW_CERT_FROM_HISTORY]    = toQString(this->allowIncoming);
    details[ConfProperties::ALLOW_CERT_FROM_CONTACT]    = toQString(this->allowIncoming);
    details[ConfProperties::ALLOW_CERT_FROM_TRUSTED]    = toQString(this->allowIncoming);
    details[ConfProperties::ARCHIVE_PASSWORD]           = toQString(this->archivePassword);
    details[ConfProperties::ARCHIVE_HAS_PASSWORD]       = toQString(this->archiveHasPassword);
    details[ConfProperties::ARCHIVE_PATH]               = toQString(this->archivePath);
    details[ConfProperties::ARCHIVE_PIN]                = toQString(this->archivePin);
    // ConfProperties::DEVICE_NAME name is set with NewDeviceModel interface
    details[ConfProperties::PROXY_ENABLED]              = toQString(this->proxyEnabled);
    details[ConfProperties::PROXY_SERVER]               = toQString(this->proxyServer);
    details[ConfProperties::PROXY_PUSH_TOKEN]           = toQString(this->proxyPushToken);
    details[ConfProperties::DHT_PEER_DISCOVERY]         = toQString(this->peerDiscovery);
    details[ConfProperties::ACCOUNT_PEER_DISCOVERY]     = toQString(this->accountDiscovery);
    details[ConfProperties::ACCOUNT_PUBLISH]            = toQString(this->accountPublish);
    // Audio
    details[ConfProperties::Audio::PORT_MAX]            = toQString(this->Audio.audioPortMax);
    details[ConfProperties::Audio::PORT_MIN]            = toQString(this->Audio.audioPortMin);
    // Video
    details[ConfProperties::Video::ENABLED]             = toQString(this->Video.videoEnabled);
    details[ConfProperties::Video::PORT_MAX]            = toQString(this->Video.videoPortMax);
    details[ConfProperties::Video::PORT_MIN]            = toQString(this->Video.videoPortMin);
    // STUN
    details[ConfProperties::STUN::SERVER]               = toQString(this->STUN.server);
    details[ConfProperties::STUN::ENABLED]              = toQString(this->STUN.enable);
    // TURN
    details[ConfProperties::TURN::SERVER]               = toQString(this->TURN.server);
    details[ConfProperties::TURN::ENABLED]              = toQString(this->TURN.enable);
    details[ConfProperties::TURN::SERVER_UNAME]         = toQString(this->TURN.username);
    details[ConfProperties::TURN::SERVER_PWD]           = toQString(this->TURN.password);
    details[ConfProperties::TURN::SERVER_REALM]         = toQString(this->TURN.realm);
    // Presence
    details[ConfProperties::Presence::SUPPORT_PUBLISH]  = toQString(this->Presence.presencePublishSupported);
    details[ConfProperties::Presence::SUPPORT_SUBSCRIBE] = toQString(this->Presence.presenceSubscribeSupported);
    details[ConfProperties::Presence::ENABLED]          = toQString(this->Presence.presenceEnabled);
    // Ringtone
    details[ConfProperties::Ringtone::PATH]             = toQString(this->Ringtone.ringtonePath);
    details[ConfProperties::Ringtone::ENABLED]          = toQString(this->Ringtone.ringtoneEnabled);
    // SRTP
    details[ConfProperties::SRTP::KEY_EXCHANGE]         = this->SRTP.keyExchange == account::KeyExchangeProtocol::NONE? "" : "sdes";
    details[ConfProperties::SRTP::ENABLED]              = toQString(this->SRTP.enable);
    details[ConfProperties::SRTP::RTP_FALLBACK]         = toQString(this->SRTP.rtpFallback);
    // TLS
    details[ConfProperties::TLS::LISTENER_PORT]         = toQString(this->TLS.listenerPort);
    details[ConfProperties::TLS::ENABLED]               = toQString(this->TLS.enable);
    details[ConfProperties::TLS::PORT]                  = toQString(this->TLS.port);
    details[ConfProperties::TLS::CA_LIST_FILE]          = toQString(this->TLS.certificateListFile);
    details[ConfProperties::TLS::CERTIFICATE_FILE]      = toQString(this->TLS.certificateFile);
    details[ConfProperties::TLS::PRIVATE_KEY_FILE]      = toQString(this->TLS.privateKeyFile);
    details[ConfProperties::TLS::PASSWORD]              = toQString(this->TLS.password);
    switch (this->TLS.method) {
    case account::TlsMethod::TLSv1:
        details[ConfProperties::TLS::METHOD]            = "TLSv1";
        break;
    case account::TlsMethod::TLSv1_1:
        details[ConfProperties::TLS::METHOD]            = "TLSv1.1";
        break;
    case account::TlsMethod::TLSv1_2:
        details[ConfProperties::TLS::METHOD]            = "TLSv1.2";
        break;
    case account::TlsMethod::DEFAULT:
    default:
        details[ConfProperties::TLS::METHOD]            = "Default";
        break;
    }
    details[ConfProperties::TLS::CIPHERS]               = toQString(this->TLS.ciphers);
    details[ConfProperties::TLS::SERVER_NAME]           = toQString(this->TLS.serverName);
    details[ConfProperties::TLS::VERIFY_SERVER]         = toQString(this->TLS.verifyServer);
    details[ConfProperties::TLS::VERIFY_CLIENT]         = toQString(this->TLS.verifyClient);
    details[ConfProperties::TLS::REQUIRE_CLIENT_CERTIFICATE] = toQString(this->TLS.requireClientCertificate);
    details[ConfProperties::TLS::NEGOTIATION_TIMEOUT_SEC] = toQString(this->TLS.negotiationTimeoutSec);
    // DHT
    details[ConfProperties::DHT::PORT]                  = toQString(this->DHT.port);
    details[ConfProperties::DHT::PUBLIC_IN_CALLS]       = toQString(this->DHT.PublicInCalls);
    details[ConfProperties::DHT::ALLOW_FROM_TRUSTED]    = toQString(this->DHT.AllowFromTrusted);
    // RingNS
    details[ConfProperties::RingNS::URI]                = toQString(this->RingNS.uri);
    details[ConfProperties::RingNS::ACCOUNT]            = toQString(this->RingNS.account);
    // Registration
    details[ConfProperties::Registration::EXPIRE]       = toQString(this->Registration.expire);

    return details;
}

std::string
NewAccountModel::createNewAccount(profile::Type type,
                                  const std::string& displayName,
                                  const std::string& archivePath,
                                  const std::string& password,
                                  const std::string& pin,
                                  const std::string& uri)
{

    MapStringString details = type == profile::Type::SIP?
                              ConfigurationManager::instance().getAccountTemplate("SIP") :
                              ConfigurationManager::instance().getAccountTemplate("RING");
    using namespace DRing::Account;
    details[ConfProperties::TYPE] = type == profile::Type::SIP? "SIP" : "RING";
    details[ConfProperties::DISPLAYNAME] = displayName.c_str();
    details[ConfProperties::ALIAS] = displayName.c_str();
    details[ConfProperties::UPNP_ENABLED] = "true";
    details[ConfProperties::ARCHIVE_PASSWORD] = password.c_str();
    details[ConfProperties::ARCHIVE_PIN] = pin.c_str();
    details[ConfProperties::ARCHIVE_PATH] = archivePath.c_str();
    if (type == profile::Type::SIP) {
        details[ConfProperties::USERNAME] = uri.c_str();
    }

    QString accountId = ConfigurationManager::instance().addAccount(details);
    return accountId.toStdString();
}



std::string
NewAccountModel::connectToAccountManager(const std::string& username,
                                         const std::string& password,
                                         const std::string& serverUri)
{
    MapStringString details = ConfigurationManager::instance().getAccountTemplate("RING");
    using namespace DRing::Account;
    details[ConfProperties::TYPE] = "RING";
    details[ConfProperties::USERNAME] = username.c_str();
    details[ConfProperties::ARCHIVE_PASSWORD] = password.c_str();
    details[ConfProperties::MANAGER_URI] = serverUri.c_str();

    QString accountId = ConfigurationManager::instance().addAccount(details);
    return accountId.toStdString();
}

void
NewAccountModel::setTopAccount(const std::string& accountId)
{
    bool found = false;
    std::string order = {};

    const QStringList accountIds = ConfigurationManager::instance().getAccountList();
    for (auto& id : accountIds)
    {
        if (id.toStdString() == accountId) {
            found = true;
        } else {
            order += id.toStdString() + "/";
        }
    }
    if (found) {
        order = accountId + "/" + order;
    }
    ConfigurationManager::instance().setAccountsOrder(order.c_str());
}

std::string
NewAccountModel::accountVCard(const std::string& accountId, bool compressImage) const
{
    auto account = pimpl_->accounts.find(accountId);
    if (account == pimpl_->accounts.end()) {
        return {};
    }
    auto& accountInfo = account->second.first;
    return authority::storage::vcard::profileToVcard(accountInfo.profileInfo, compressImage);
}

} // namespace lrc

#include "api/moc_newaccountmodel.cpp"
#include "newaccountmodel.moc"

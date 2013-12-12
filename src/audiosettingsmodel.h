/****************************************************************************
 *   Copyright (C) 2013 by Savoir-Faire Linux                               *
 *   Author : Emmanuel Lepage Vallee <emmanuel.lepage@savoirfairelinux.com> *
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
#ifndef AUDIOSETTINGSMODEL_H
#define AUDIOSETTINGSMODEL_H

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QAbstractListModel>

#include "typedefs.h"

class AlsaPluginModel    ;
class InputDeviceModel   ;
class OutputDeviceModel  ;
class AudioManagerModel   ;
class RingtoneDeviceModel;

/**
 * This class group all ComboBox models used by audio settings dialogs
 */
class LIB_EXPORT AudioSettingsModel : public QObject {
   Q_OBJECT
public:

   enum DeviceIndex {
      OUTPUT   = 0,
      INPUT    = 1,
      RINGTONE = 2,
   };

   virtual ~AudioSettingsModel();
   static AudioSettingsModel* instance();

   //Getters
   AlsaPluginModel*     alsaPluginModel    ();
   InputDeviceModel*    inputDeviceModel   ();
   OutputDeviceModel*   outputDeviceModel  ();
   AudioManagerModel*   audioManagerModel  ();
   RingtoneDeviceModel* ringtoneDeviceModel();
   bool                 isRoomToneEnabled  ();
   bool                 isNoiseSuppressEnabled () const;
   bool                 isPlaybackMuted() const;
   bool                 isCaptureMuted () const;
   int                  playbackVolume () const;
   int                  captureVolume  () const;

   //Setters
   void setEnableRoomTone    ( bool enable  );
   void setNoiseSuppressState( bool enabled );

   //Room tone type
   enum class ToneType {
      WITHOUT_MESSAGE = 0,
      WITH_MESSAGE    = 1,
   };

   class DeviceKey {
   public:
      constexpr static const char* CAPTURE  = "mic"    ;
      constexpr static const char* PLAYBACK = "speaker";
   };

   //Mutator
   ToneType playRoomTone() const;
   void     stopRoomTone() const;

public Q_SLOTS:
   void reload();
   void mutePlayback(bool m);
   void muteCapture (bool m);
   void setPlaybackVolume(int volume);
   void setCaptureVolume(int volume);

Q_SIGNALS:
   void captureMuted(bool);
   void playbackMuted(bool);
   void playbackVolumeChanged(int);
   void captureVolumeChanged(int);

private:
   //Constructor
   explicit AudioSettingsModel();

   //Attributes
   AlsaPluginModel*     m_pAlsaPluginModel    ;
   InputDeviceModel*    m_pInputDeviceModel   ;
   OutputDeviceModel*   m_pOutputDeviceModel  ;
   AudioManagerModel*   m_pAudioManagerModel  ;
   RingtoneDeviceModel* m_pRingtoneDeviceModel;
   bool                 m_EnableRoomTone      ;

   //Singleton
   static AudioSettingsModel* m_spInstance;
};

class LIB_EXPORT AlsaPluginModel    : public QAbstractListModel {
   Q_OBJECT
public:
   explicit AlsaPluginModel(QObject* parent);
   virtual ~AlsaPluginModel();

   //Models function
   virtual QVariant      data    ( const QModelIndex& index, int role = Qt::DisplayRole ) const;
   virtual int           rowCount( const QModelIndex& parent = QModelIndex()            ) const;
   virtual Qt::ItemFlags flags   ( const QModelIndex& index                             ) const;
   virtual bool          setData ( const QModelIndex& index, const QVariant &value, int role);

   //Getters
   QModelIndex currentPlugin() const;

   //Setters
   void setCurrentPlugin(const QModelIndex& idx);
   void setCurrentPlugin(int idx);

   //Mutator
   void reload();

private:
   QStringList m_lDeviceList;
};

class LIB_EXPORT InputDeviceModel   : public QAbstractListModel {
   Q_OBJECT
public:
   explicit InputDeviceModel(QObject* parent);
   virtual ~InputDeviceModel();

   //Models function
   virtual QVariant      data    ( const QModelIndex& index, int role = Qt::DisplayRole ) const;
   virtual int           rowCount( const QModelIndex& parent = QModelIndex()            ) const;
   virtual Qt::ItemFlags flags   ( const QModelIndex& index                             ) const;
   virtual bool          setData ( const QModelIndex& index, const QVariant &value, int role);

   //Getters
   QModelIndex currentDevice() const;

   //Setters
   void setCurrentDevice(const QModelIndex& index);
   void setCurrentDevice(int idx);

   //Mutator
   void reload();

private:
   QStringList m_lDeviceList;
};

class LIB_EXPORT OutputDeviceModel  : public QAbstractListModel {
   Q_OBJECT
public:
   explicit OutputDeviceModel(QObject* parent);
   virtual ~OutputDeviceModel();

   //Models function
   virtual QVariant      data    ( const QModelIndex& index, int role = Qt::DisplayRole ) const;
   virtual int           rowCount( const QModelIndex& parent = QModelIndex()            ) const;
   virtual Qt::ItemFlags flags   ( const QModelIndex& index                             ) const;
   virtual bool          setData ( const QModelIndex& index, const QVariant &value, int role);

   //Getters
   QModelIndex currentDevice() const;

   //Setters
   void setCurrentDevice(const QModelIndex& index);
   void setCurrentDevice(int idx);

   //Mutator
   void reload();

private:
   QStringList m_lDeviceList;
};

class LIB_EXPORT AudioManagerModel   : public QAbstractListModel {
   Q_OBJECT
public:
   explicit AudioManagerModel(QObject* parent);
   virtual ~AudioManagerModel();

   //Models function
   virtual QVariant      data    ( const QModelIndex& index, int role = Qt::DisplayRole ) const;
   virtual int           rowCount( const QModelIndex& parent = QModelIndex()            ) const;
   virtual Qt::ItemFlags flags   ( const QModelIndex& index                             ) const;
   virtual bool          setData ( const QModelIndex& index, const QVariant &value, int role);

   //Getters
   QModelIndex currentManager() const;

   //Setters
   void setCurrentManager(const QModelIndex& index);

public Q_SLOTS:
   void setCurrentManager(int idx);


private:
   QStringList m_lDeviceList;
   class ManagerName {
   public:
      constexpr static const char* PULSEAUDIO = "pulseaudio";
      constexpr static const char* ALSA       = "alsa";
   };

   enum class Manager {
      ALSA =0,
      PULSE=1,
   };
};

class LIB_EXPORT RingtoneDeviceModel: public QAbstractListModel {
   Q_OBJECT
public:
   explicit RingtoneDeviceModel(QObject* parent);
   virtual ~RingtoneDeviceModel();

   //Models function
   virtual QVariant      data    ( const QModelIndex& index, int role = Qt::DisplayRole ) const;
   virtual int           rowCount( const QModelIndex& parent = QModelIndex()            ) const;
   virtual Qt::ItemFlags flags   ( const QModelIndex& index                             ) const;
   virtual bool          setData ( const QModelIndex& index, const QVariant &value, int role);

   //Getters
   QModelIndex currentDevice() const;

   //Setters
   void setCurrentDevice(const QModelIndex& index);
   void setCurrentDevice(int idx);

   //Mutator
   void reload();

private:
   QStringList m_lDeviceList;
};

#endif //AUDIOSETTINGSMODEL_H

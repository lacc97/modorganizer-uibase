/*
This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "registry.h"

#if not defined(WIN32)
#   include <fstream>

#   include <boost/algorithm/string/case_conv.hpp>
#   include <boost/algorithm/string/trim.hpp>

#   include <QFile>
#   include <QFileInfo>
#endif

#include <QString>
#include <QMessageBox>
#include <QApplication>
#include <QtCore/QFileDevice>

namespace MOBase {

#if defined(WIN32)
bool WriteRegistryValue(LPCWSTR appName, LPCWSTR keyName, LPCWSTR value, LPCWSTR fileName)
{
  bool success = true;
  if (!::WritePrivateProfileString(appName, keyName, value, fileName)) {
    success = false;
    switch(::GetLastError()) {
      case ERROR_ACCESS_DENIED:
      {
        DWORD attrs = ::GetFileAttributes(fileName);
        if ((attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_READONLY)) {
          if (QMessageBox::question(QApplication::activeModalWidget(),QApplication::tr("INI file is read-only"),
                QApplication::tr("Mod Organizer is attempting to write to \"%1\" which is currently set to read-only. "
                "Clear the read-only flag to allow the write?").arg(fileName)) == QMessageBox::Yes) {
            qWarning(QString("%1 is read-only.  Attempting to clear read-only flag.").arg(fileName).toLocal8Bit());
            attrs &= ~(FILE_ATTRIBUTE_READONLY);
            if (::SetFileAttributes(fileName, attrs)) {
              if (::WritePrivateProfileString(appName, keyName, value, fileName)) {
                success = true;
              }
            }
          } else {
            qWarning(QString("%1 is read-only.  User denied clearing the read-only flag.").arg(fileName).toLocal8Bit());
          }
        }
      } break;
    }
  }

  return success;
}
#else
    class INIConfiguration {
        class Section {
            friend class INIConfiguration;

            struct Property {
                std::string m_Name;
                std::string m_Value;
            };

            typedef std::map<std::string, Property> property_map;
        public:
            Section(const Section& s) = default;

            Section(Section&& s) = default;

        private:
            explicit Section(std::string  sName): m_Name(std::move(sName)), m_PropertyMap() {}

            std::string m_Name;
            property_map m_PropertyMap;
        };

        typedef std::map<std::string, Section> section_map;
    public:
        bool load(std::istream& inStream)
        {
            if(!inStream)
                return false;

            std::string currSectionName;

            std::string line;
            std::getline(inStream, line);

            while(inStream && !inStream.eof()) {
                if(line[0] == '[') {
                    currSectionName = line.substr(1, line.find_last_of(']') - 1);
                    addSection(currSectionName);
                } else if(line[0] != '#' && line.length() > 2) {
                    unsigned long crloc = line.length() - 1;

                    if(crloc >= 0 && line[crloc] == '\r') //check for Windows-style newline
                        line = line.substr(0, crloc); //and correct

                    std::string key, val;

                    if(parseProperty(line, key, val))
                        addProperty(currSectionName, key, val);
                }

                std::getline(inStream, line);
            }

            return bool(inStream);
        }

        bool save(std::ostream& outStream) const
        {
            if(!outStream.good())
                return false;

            std::for_each(m_SectionMap.begin(), m_SectionMap.end(), [&](const std::pair<std::string, Section>& selem) {
                if(!selem.second.m_Name.empty())
                    outStream << "[" << selem.second.m_Name << "]" << std::endl;

                std::for_each(selem.second.m_PropertyMap.begin(), selem.second.m_PropertyMap.end(),
                              [&](const Section::property_map::value_type& pelem) {
                                  outStream << pelem.second.m_Name << "=" << pelem.second.m_Value << std::endl;
                              });

                outStream << std::endl;
            });

            return true;
        }

        void addSection(const std::string& sName)
        {
            if(m_SectionMap.find(boost::to_lower_copy(sName)) == m_SectionMap.end())
                m_SectionMap.emplace(boost::to_lower_copy(sName), Section(sName));
        }

        void setStringProperty(const std::string& sName, const std::string& name, const std::string& val)
        {
            addSection(sName);
            addProperty(sName, name, val);
        }

    protected:
        static bool parseProperty(const std::string& raw, std::string& key, std::string& val)
        {
            size_t equalsPos = raw.find_first_of('=');

            if(equalsPos != std::string::npos) {
                key = raw.substr(0, equalsPos);
                val = raw.substr(equalsPos + 1);

                if(val[0] == '"' || val[0] == '\'')
                    val = val.substr(1);

                if(val[val.length() - 1] == '"' || val[val.length() - 1] == '\'')
                    val.resize(val.length() - 1);

                return true;
            }

            return false;
        }

        void addProperty(const std::string& sName, const std::string& name, const std::string& val)
        {
            Section::Property p;
            p.m_Name = boost::trim_copy(name);
            p.m_Value = boost::trim_copy(val);

            m_SectionMap.at(boost::to_lower_copy(sName)).m_PropertyMap[boost::to_lower_copy(p.m_Name)] = p;
        }

    private:
        section_map m_SectionMap;
    };

bool WriteRegistryValue(const wchar_t *appName, const wchar_t *keyName, const wchar_t *value,
                                const wchar_t *fileName)
{
    QFile file(QString::fromStdWString(fileName));
    QFileDevice::Permissions permissions = file.permissions();

    if(!permissions.testFlag(QFileDevice::ReadUser)) {
        qWarning(QStringLiteral("%1 is not readable by current user.  Attempting to set read flag.").arg(QString::fromStdWString(fileName)).toLocal8Bit());

        permissions.setFlag(QFileDevice::ReadUser);

        if(!file.setPermissions(permissions)) {
            qWarning(QStringLiteral("%1 is not readable by current user.  Failed to set read flag.").arg(QString::fromStdWString(fileName)).toLocal8Bit());
            return false;
        }
    }

    if(!permissions.testFlag(QFileDevice::WriteUser)) {
        if (QMessageBox::question(QApplication::activeModalWidget(),QApplication::tr("INI file is read-only"),
                                  QApplication::tr("Mod Organizer is attempting to write to \"%1\" which is currently set to read-only. "
                                                   "Clear the read-only flag to allow the write?").arg(QString::fromStdWString(fileName))) == QMessageBox::Yes) {
            qWarning(QStringLiteral("%1 is read-only.  Attempting to set write flag.").arg(QString::fromStdWString(fileName)).toLocal8Bit());

            permissions.setFlag(QFileDevice::WriteUser);

            if(!file.setPermissions(permissions)) {
                qWarning(QStringLiteral("%1 is read-only.  Failed to set write flag.").arg(QString::fromStdWString(fileName)).toLocal8Bit());
                return false;
            }
        } else {
            qWarning(QStringLiteral("%1 is read-only.  User denied setting the write flag.").arg(QString::fromStdWString(fileName)).toLocal8Bit());
            return false;
        }
    }

    INIConfiguration privateProfile;

    {
        std::ifstream is(QFileInfo(file).path().toStdString());

        if (!privateProfile.load(is))
            return false;
    }

    privateProfile.setStringProperty(QString::fromStdWString(appName).toStdString(),
            QString::fromStdWString(keyName).toStdString(), QString::fromStdWString(value).toStdString());

    std::ofstream os(QFileInfo(file).path().toStdString());
    return privateProfile.save(os);
}
#endif

} // namespace MOBase

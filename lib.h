#ifndef LIB_H
#define LIB_H

#include "ThostFtdcUserApiStruct.h"
#include "iniReader/iniReader.h"
#include <string>
#include <time.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace std;

const int PATH_LOG  = 1;
const int PATH_PID  = 2;
const int PATH_DATA = 3;

class Lib
{
public:

    static string getDate(string format)
    {
        time_t t = time(0);
        char tmp[20];
        strftime(tmp, sizeof(tmp), format.c_str(), localtime(&t));
        string s(tmp);
        return s;
    };

    static string getPath(const string pathName, int type = PATH_LOG)
    {
        string date, root;
        string logPath  = getOptionToString("log_path");
        string appRoot  = getOptionToString("app_root");
        string dataPath = getOptionToString("data_path");

        switch (type) {
            case PATH_LOG:
                date = getDate("%Y%m%d");
                return logPath + pathName + "_" + date + ".log";
            case PATH_PID:
                return appRoot + "pid";
            case PATH_DATA:
                date = getDate("%Y%m%d");
                return dataPath + pathName + "_" + date + ".log";
            default:
                break;
        }
        return "";
    };

    static char * stoc(string str)
    {
        const char * s = str.c_str();
        char * ch = new char[strlen(s) + 1];
        strcpy(ch, s);
        return ch;
    }

    static void sysErrLog(string logName, CThostFtdcRspInfoField *info, int id, int isLast)
    {
        string sysPath = getPath("sys");
        ofstream sysLogger;
        sysLogger.open(sysPath.c_str(), ios::app);

        sysLogger << getDate("%Y-%m-%d %H:%M:%S") << "|";
        sysLogger << "[ERROR]" << "|";
        sysLogger << logName << "|";
        sysLogger << "ErrCode" << "|" << info->ErrorID << "|";
        sysLogger << "ErrMsg" << "|" << info->ErrorMsg << "|";
        sysLogger << "RequestID" << "|" << id << "|";
        sysLogger << "IsLast" << "|" << isLast << endl;

        sysLogger.close();
    }

    static void sysReqLog(string logName, int code)
    {
        string sysPath = getPath("sys");

        ofstream sysLogger;
        sysLogger.open(sysPath.c_str(), ios::app);

        sysLogger << getDate("%Y-%m-%d %H:%M:%S") << "|";
        sysLogger << "[REQUEST]" << "|";
        sysLogger << logName << "|";
        sysLogger << "Code" << "|" << code << endl;

        sysLogger.close();
    }

    static void initInfoLogHandle(ofstream & sysLogger)
    {
        string sysPath = getPath("sys");

        sysLogger.open(sysPath.c_str(), ios::app);

        sysLogger << getDate("%Y-%m-%d %H:%M:%S") << "|";
        sysLogger << "[INFO]" << "|";
    }

    static vector<string> split(const string& s, const string& delim)
    {
        vector<string> elems;
        size_t pos = 0;
        size_t len = s.length();
        size_t delim_len = delim.length();
        if (delim_len == 0) return elems;
        while (pos < len)
        {
            int find_pos = s.find(delim, pos);
            if (find_pos < 0)
            {
                elems.push_back(s.substr(pos, len - pos));
                break;
            }
            elems.push_back(s.substr(pos, find_pos - pos));
            pos = find_pos + delim_len;
        }
        return elems;
    }

    Lib();
    ~Lib();

};








#endif

#include <fstream>
#include <iomanip>
#include <iostream>

#include "server_manager.h"
#include "ftp_command_parser.h"
#include "command_support.h"
#include "server_constants.h"

using std::ifstream;
using std::iostream;

ServerManager::ServerManager(){
    pthread_mutex_init(&users_map_mutex, NULL);
}

ServerManager::~ServerManager(){
    pthread_mutex_destroy(&users_map_mutex);
}

void ServerManager::prepare_response_with_code(string* prepared_response, const string& file_path, ResponseCode code,map<string,string>& parameters) {
    ifstream input_file(file_path.c_str());
    string prepared_string = string((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
    if (parameters.size() != 0){
        map<string,string>::iterator it;
        for (it = parameters.begin() ;it!=parameters.end();++it){
            size_t index = prepared_string.find(it->first);
            if (index != string::npos)
                prepared_string.replace(index,(it->first).length(),it->second);
        }
    }
    ResponseCode response_code(code);
    HttpResponseBuilder response_builder(response_code);
    response_builder.setBody(prepared_string);
    *prepared_response = response_builder.getSerialization();
}

void ServerManager::prepare_response_from_file(string* prepared_response, const string& file_path,map<string,string>& parameters) {
    ResponseCode code = ResponseCode(OK);
    prepare_response_with_code(prepared_response, file_path, code,parameters);
}

void ServerManager::handle_request(string* response, const string& request_data) {
    HttpGetRequestParser get_request_parser(request_data);
    string required_file_name = get_request_parser.getRequiredFileName();
    map<string,string> parameters;
    if (required_file_name == "/login.html") {
        prepare_response_from_file(response, string(LOGIN_HTML),parameters);
    } else if(required_file_name == "/login.php") {
        handle_login(response, request_data);
    } else if (required_file_name == "/signup.html") {
        prepare_response_from_file(response, string(SIGNUP_HTML),parameters);
    } else if (required_file_name == "/signup.php") {
        VALIDATION_CODE validation_code = can_add_user(request_data);
        if (validation_code == VALID) {
            prepare_response_from_file(response, LOGIN_HTML, parameters);
        } else if (validation_code == USED_USERNAME) {
            prepare_response_from_file(response, INVALID_USERNAME_HTML, parameters);
        } else if (validation_code == MISSING_FIELD) {
            prepare_response_from_file(response, MISSING_FIELD_HTML, parameters);
        } else {
            prepare_response_from_file(response, WRONG_TYPE_HTML, parameters);
        }
    } else {
        ResponseCode code(NOT_FOUND);
        prepare_response_with_code(response, NOTFOUND_HTML, code,parameters);
    }
}

void ServerManager::handle_login(string* response, const string& request_data) {
    HttpGetRequestParser get_request_parser(request_data);
    map<string,string> parameters;
    if (valid_user(request_data)) {
        string username = get_request_parser.getParameter(USER_NAME);
        username = username.substr(1,username.length()-2);
        string password = get_request_parser.getParameter(PASSWORD);
        password = password.substr(1,password.length()-2);
        if (users_map_[get_request_parser.getParameter(USER_NAME)].getType() == "\"voter\""){
            parameters["FTP_LINK"] = "ftp://anonymous:" + username + "@localhost/";
            prepare_response_from_file(response, string("../htdocs/voter_home.html"),parameters);
        } else {
            parameters["FTP_LINK"] = "ftp://" + username + ":" + password +
                "@localhost/" + username + "/";
            prepare_response_from_file(response, string("../htdocs/candidate_home.html"),parameters);
        }
    } else {
        prepare_response_from_file(response, string("../htdocs/invalid_data.html"),parameters);
    }
}

bool ServerManager::valid_user(const string& request_data) {
    HttpGetRequestParser get_request_parser(request_data);
    if(users_map_.count(get_request_parser.getParameter(USER_NAME)) == 0)
        return false;
    User system_user = users_map_[get_request_parser.getParameter(USER_NAME)];
    if(get_request_parser.getParameter(PASSWORD) != system_user.getPassword())
        return false;
    return true;
}

VALIDATION_CODE ServerManager::can_add_user(const string& request_data) {
    HttpGetRequestParser get_request_parser(request_data);
    pthread_mutex_lock(&users_map_mutex);
    // Extract parameters from GET request
    string username = get_request_parser.getParameter(USER_NAME);
    string password = get_request_parser.getParameter(PASSWORD);
    string type = get_request_parser.getParameter("type");
    // check for empty parameters.
    if (username == "\"\"" || password == "\"\"" || type == "\"\"") {
        pthread_mutex_unlock(&users_map_mutex);
        return MISSING_FIELD;
    }
    // type should be either a voter or a candidate.
    if (type != "\"voter\"" && type != "\"candidate\"") {
        pthread_mutex_unlock(&users_map_mutex);
        return WRONG_TYPE;
    }
    if(users_map_.count(username) == 0) {
        User new_user(type, username, password);
        users_map_[username] = new_user;
        pthread_mutex_unlock(&users_map_mutex);
        return VALID;
    } else {
	pthread_mutex_unlock(&users_map_mutex);
        return USED_USERNAME;
    }
}

bool ServerManager::handle_ftp_command(string* response, const string& command_data, ftp_state & state) {
    // Parse the command sent from server.
    FtpCommandParser command_parser;
    command_parser.parse_command(command_data);
    string head, body;
    command_parser.get_command_head(&head);
    command_parser.get_command_body(&body);
    // Handle the different commands.
    CommandSupporter command_supporter;
    if (head == LIST) {
        // Handle the list command
        *response = command_supporter.ls(body);
    } else if (head == MKD) {
        if (command_supporter.mkdir(body)) {
            *response = COMMAND_OK;
        } else {
            *response = SYNTAX_ERROR;
        }
    } else if (head == CWD) {
        // TODO: CHECK PARAMETERS WITH KOTB
        *response = command_supporter.cd(body, body);
    } else if (head == RMD) {
        command_supporter.rm(body);
    } else if (head == USER) {
        state.username = body;
        *response = USER_NAME_OK;
    }else if (head == PASS){
        if (users_map_.count(state.username) == 0)
            *response = INVALID_AUTHENTICATION;
        else{
            User u = users_map_[state.username];
            if (u.getPassword() != body)
                *response = INVALID_AUTHENTICATION;
            else
                *response = COMMAND_OK;
        }
    }else if (head == BYE){
        *response = "Bye";
        return false;
    }
    return true;
    // handle rest of commands here
}

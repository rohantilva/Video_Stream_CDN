#include <iostream>
#include <map>
#include <string>
#include <cstring>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <chrono>
#include <ctime>
#include <ratio>
#include <algorithm>
#include <fstream>

using namespace std;
using namespace std::chrono;

int get_seg(string buffer, string ya) {
    size_t goal = buffer.find(ya);
    if (goal == string::npos) {
        return -1;
    } else {
        string small = buffer.substr(goal + 3);
        size_t end = small.find("-");
        return atoi(small.substr(0, end).c_str());
    } 
}

int get_frag(string buffer, string ya) {
    size_t goal = buffer.find(ya);
    if (goal == string::npos) {
        return -1;
    } else {
        string small = buffer.substr(goal + 4);
        size_t end = small.find(" ");
        return atoi(small.substr(0, end).c_str());
    }
}

int header_length(char* buf) {
    char* temp = strstr(buf, "\r\n\r\n");
    return (int) (temp + 4 - buf);
}

int _content(char* buf){
    char* temp = strstr(buf, "Content-Length: ");
    return (int) atoi(temp + 16);
}

vector<int> getBitrates(const string& xml) {
    using std::string;
    string keyword = "bitrate=\"";
    vector<int> lst;
    for(size_t tagPos = xml.find("<media"); tagPos != string::npos; tagPos = xml.find("<media", tagPos + 1)) {
        size_t keyPos = xml.find(keyword, tagPos);
        if(keyPos == string::npos)
            continue;
        int bitrateLoc = keyPos + keyword.size();
        int len = xml.find('"', bitrateLoc) - bitrateLoc;
        int bitrate = stoi(xml.substr(bitrateLoc, len));
        lst.push_back(bitrate);
    }
    return lst;
}

int main(int argc, char **argv) {
    //./miProxy <log> <alpha> <listen-port> [<www-ip>]
    if (argc != 5) {
        cerr << "Command line arguments are not correct." << endl;
    	return 1;
    }
    string log(argv[1]);
    float alpha = atof(argv[2]);
    char * listen_port = argv[3];
    string www_ip(argv[4]);

    int _sockfd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo _hints, *_servinfo, *_p;
    int _rv; 
    memset(&_hints, 0, sizeof _hints);
    _hints.ai_family = AF_UNSPEC;
    _hints.ai_socktype = SOCK_STREAM;
    _hints.ai_flags = AI_PASSIVE; // use my IP

    int numbytes;  
    if ((_rv = getaddrinfo(www_ip.c_str(), "80", &_hints, &_servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(_rv));
        return 1;
    }

    for(_p = _servinfo; _p != nullptr; _p = _p->ai_next) {
        if ((_sockfd = socket(_p->ai_family, _p->ai_socktype,
                _p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(_sockfd, _p->ai_addr, _p->ai_addrlen) == -1) {
            close(_sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int rv;

    struct addrinfo hints, *ai, *p;

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    rv = getaddrinfo(NULL, listen_port, &hints, &ai);
    
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    freeaddrinfo(ai); // all done with this

    listen(listener, 10);

    addrlen = sizeof remoteaddr;
    int seg, frag  = 0;
    double bitrate, T_new, T_cur = 0;
    vector<int> bitrates;
    chrono::time_point<chrono::system_clock> start, finish;
    chrono::duration<double> total_time;
    int recvd, stop = 0;    
    double bitrate_max = 0;
    char* buf = (char *) calloc(16000, sizeof(char));
    char* buf2 = (char *) calloc(16000, sizeof(char));
    
    ofstream logfile;
    logfile.open(log);
    logfile << "";
    logfile.close();

    string chunkname = "";
    while (true) {
        int total_bytes = 0;
        int newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);
        recvd = recv(newfd, buf, 16000, 0); // receive from browser

        string buf_edit = buf;        
        seg = get_seg(buf_edit, "Seg");
        frag = get_frag(buf_edit, "Frag"); 
        
        if (seg != -1 && frag != -1) {
            start = chrono::system_clock::now();
            bitrate_max = T_cur / 1.5;
            
            for (int i = bitrates.size() - 1; i >= 0; --i) {
                if (bitrates[i] <= bitrate_max) {
                    bitrate = bitrates[i];
                    break;
                }
            }

            if ((int) bitrate == 0) {
                bitrate = bitrates[0];
            }
            
            string before_seg = buf_edit.substr(0, buf_edit.find("Seg"));
            stop = 0;
            for (int i = before_seg.length() - 1; i >= 0; --i) {
                if (before_seg[i] == '/') {
                    stop = i;
                    break;
                }
            }
            int temp1 = buf_edit.size();
            buf_edit.replace(stop + 1, buf_edit.find("Seg") - stop - 1, to_string((int)bitrate));
            int temp2 = buf_edit.size();
            strncpy(buf, buf_edit.c_str(), 16000);
            recvd = recvd + (temp2 - temp1);  
        
            size_t beg = buf_edit.find("/");
            string tempp = buf_edit.substr(beg);
            size_t endd = tempp.find(" ");
            chunkname = tempp.substr(0, endd);
        }
		
        size_t tem = buf_edit.find(".f4m");
        int og_recvd = recvd;
        if (tem != string::npos){
            send(_sockfd, buf, recvd, 0);
            recvd = recv(_sockfd, buf2, 16000, 0);
            bitrates = getBitrates(string(buf2));
            T_cur = *min_element(bitrates.begin(), bitrates.end());
            buf_edit.insert(tem, "_nolist");
            strncpy(buf, buf_edit.c_str(), 16000);
            recvd = og_recvd + 7;
        }
    
        int temp = send(_sockfd, buf, recvd, 0); //send to server
        int recvd = recv(_sockfd, buf2, 16000, 0); // receive back from server
        int header = header_length(buf2);
        int content = _content(buf2);
        int left = content + header - recvd;
        send(newfd, buf2, recvd, 0); //send back to browser
        
        while (left != 0){
            recvd = recv(_sockfd, buf2, min(left, 16000), 0);
            left -= recvd;
            send(newfd, buf2, recvd, 0);
	}      

        if (seg != -1 && frag != -1) {
            finish = chrono::system_clock::now();
            total_time = finish - start;
            T_new = content * 8/(total_time.count() * 1000);
            T_cur = alpha * T_new + (1 - alpha) * T_cur;
            logfile.open(log, ios_base::app);
            logfile << total_time.count() << " ";
            logfile << T_new << " " ;
            logfile << T_cur << " ";
            logfile << bitrate << " ";
            logfile << www_ip << " ";
            logfile << chunkname;
            logfile << "\n";
            logfile.close();
        }

        close(newfd);
    }
    free(buf);

    return 0;
}

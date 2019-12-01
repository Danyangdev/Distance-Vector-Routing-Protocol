/**
 * @dluo2_assignment3
 * @author  Danyang Luo <dluo2@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */


#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include <sys/time.h>
#include <vector>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/queue.h>
#include <unistd.h>
#include <string.h>
#include <algorithm>
#define ERROR(err_msg) {perror(err_msg); exit(EXIT_FAILURE);}
static int routing_header_size = 8;
static int routing_content_size = 12;
/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
struct timeval tv_one_mimus_two(struct timeval tv1, struct timeval tv2);
int new_control_conn(int sock_index);



//定义当前host的各种信息
struct current_router{
	uint32_t my_ip;
	uint16_t my_router_port;
	uint16_t my_data_port;
	uint16_t my_id;
	uint16_t routers_number;
	uint16_t time_period;
};
struct current_router current_router;


//定义control message header && control response message header &&　data packet format　前两个个结构所使用的数据类型是一样的
struct __attribute__((__packed__)) Control_Header {
    uint32_t dest_ip;
    uint8_t control_code;
    uint8_t response_time;
    uint16_t payload_len;
};

struct __attribute__((__packed__)) Control_Response_Header {
    uint32_t controller_ip;
    uint8_t control_code;
    uint8_t response_code;
    uint16_t payload_len;
};

struct __attribute__((__packed__)) Data_Header {
    uint32_t dest_ip;
    uint8_t transfer_id;
    uint8_t ttl;
    uint16_t seq_num;
    uint32_t fin_padding;
};

//定义router header && data header
struct __attribute__((__packed__)) Data_Header {
    uint32_t dest_ip;
    uint8_t transfer_id;
    uint8_t ttl;
    uint16_t seq_num;
    uint32_t fin_padding;
};

struct __attribute__((__packed__)) Route_Header {
    uint16_t num_update;
    uint16_t source_port;
    uint32_t source_ip;
};


//当前host(路由器)的邻居也都些路由器,所以定义邻居路由器的性质
//对当前路由器定义一个list去存储与他链接的路由器们
struct NEIGHBOR_ROUTER
{
	int socket;
	uint16_t neighbor_router_ID;
	uint16_t neighbor_router_IP;
	uint32_t neighbor_router_PORT;
};
std::vector<NEIGHBOR_ROUTER> neighbor_router_list;


//定义一个超时的数据结构
//记录一个host上的与本机想链接的路由器们的超时3T的数据
struct TIMEOUT{
	int router_ID;
	int is_connected;
	struct timeval expired_time;
};
std::vector<TIMEOUT> routers_timeout_list;

//定义DV本身
//定义与当前host相链接的路由器的信息
struct DISTANCE_VECTOR
{
	uint16_t dest_ID;
	uint16_t cost;
};
struct ROUTER_INFO_ON_TABLE{
	uint16_t dest_ID;
	uint16_t dest_router_PORT;
	uint16_t dest_data_PORT;
	uint16_t dest_COST;
	uint32_t dest_IP;
	uint16_t next_hop_ID;
	std::vector<DISTANCE_VECTOR> dv_LIST;
};
std::vector<ROUTER_INFO_ON_TABLE> host_DV_table;

//给select函数用的
fd_set master_list, watch_list;
//用于第一次初始化
bool first_time_running_program;
//用于最大的那个file descriptor
int head_fd;
//设置timeout去实现timer
struct timeval waiting_time;
//这个主要在初始化的时候用了一下,因为初始化花了点时间, 需要记录初始化之后的那个时刻并加上T, 这个时刻是路由器之间每T时间间隔之后更新的时刻
struct timeval next_send;
//因为路由器之间的DV的传播有周期性的, 所以需要保存下次的发生时间,以此判断是否timeout
struct timeval next_event;
//读取control port
uint16_t CONTROL_PORT;
//定义路由器之间,更新路由表的时间间隔
uint16_t time_period_T;
//Infinity
int INF = 65535;
//储存所有的链接control socket,判断是否
std::vector<int> control_socket_list;


//定义一个data socket
//并且定义一个list去存储与当前host链接的路由器的data socket
struct DATA_SOCKET{
    uint32_t router_ip;
    int socket;
};
std::vector<DATA_SOCKET> data_socket_list;
//
int control_socket, router_socket, data_socket;





int main(int argc, char **argv)
{
    /*Start Here*/
    sscanf(argv[1], "%hu", &CONTROL_PORT);
    control_socket = create_control_sock();
    //router_socket 和 data_socket 将会被控制器的INIT指令初始化
	//两个list初始化
    FD_ZERO(&master_list);
    FD_ZERO(&watch_list);

    /* Register the control socket */
    FD_SET(control_socket, &master_list);
    head_fd = control_socket;
    main_loop();
    return 0;
}

void main_loop()
{
	//记录一次路由器的行为的开始时间
    struct timeval current_time;
	//假若一个路由器行为结束i并没有发生timeout,记录这个结束时间
	struct timeval current_event_no_timeout_happened;
	//读取select的返回值, socket-
	int selret, sock_index, fdaccept;
	//第一次运行的时候,是true；之后就是false
	first_time_running_program = 1;
	//初始化的时候,下一次事件的时间未知
	next_event.tv_sec = 0;
    while(1){
		//首先获取当前时间
		gettimeofday(&current_time,NULL);
		std::cout<<"开始时刻:　"<<current_time.tv_sec<<"."<<current_time.tv_usec/100000<<"s"<<std::endl;
        watch_list = master_list;
		if(first_time_running_program){
			//如果第一次就直接读
			//初始化所有的路由表
        	selret = select(head_fd+1, &watch_list, NULL, NULL, NULL);
		}else{
			//如果之后,所有的路由器都有路由表了
			//就要等一个周期,然后更新路由表
			//如果等超过三个周期,则remove
	        selret = select(head_fd+1, &watch_list, NULL, NULL, &waiting_time);
		}
		gettimeofday(&current_time,NULL);
		std::cout<<"完成select的时刻:　"<<current_time.tv_sec<<"."<<current_time.tv_usec/100000<<"s"<<std::endl;
        if(selret < 0)
            ERROR("select failed.");
		if(selret == 0){
			std::cout<<"此时发生超时, 可能是超时或者是断连"<<std::endl;
			gettimeofday(&current_time,NULL);
			std::cout<<"完成select的时刻:　"<<current_time.tv_sec<<"."<<current_time.tv_usec/100000<<"s"<<std::endl;
			//用当前时间-下一次更新路由表的时间
			struct timeval time_difference = tv_one_mimus_two(current_time, next_send);
			//如果这个时间差>0,但是有小于时间间隔,等于说是到达了更新的时刻,需要进行更新
			if(time_difference.tv_sec<time_period_T &&(time_difference.tv_usec>=0 || time_difference.tv_usec>=0) ){
				//开始更新
				//第一步更新下次执行更新路由表的时刻 = 当前时刻 + 时间间隔(这个时间间隔将会在command中给出)
				next_send.tv_sec=current_time.tv_sec+time_period_T;
				next_send.tv_usec = current_time.tv_usec;
				std::cout<<"下次更新路由表的时刻:　"<<next_send.tv_sec<<"."<<next_send.tv_usec/100000<<"s"<<std::endl;
				//这里要定义一个function来发送路由表到邻居
				//我要把邻居放在一个list里面,然后通过traverse来把路由表信息发给list中所有路由器
				send_DISTANCE_VECTOR();
				//gettimeofday(&current_time,NULL);
				//std::cout<<"发送玩DV的时刻:　"<<current_time.tv_sec<<"."<<current_time.tv_usec/100000<<"s"<<std::endl;
				//设置临时等待时间为下次更新时间-当前时间
				waiting_time = tv_one_mimus_two(next_send,current_time);
				std::cout<<"等待时间:　"<<waiting_time.tv_sec<<"."<<waiting_time.tv_usec/100000<<"s"<<std::endl;
			}
			//如果(当前时间-初始化时设置这个路由器的3T>0)与当前路由器想链接的路由器则设置为不再链接, 并且距离设置为无穷
			//这个时候要traverse所有的与当前路由器链接的路由器
			for(int i=0;i<routers_timeout_list.size(); i++){
				if(routers_timeout_list.at(i).is_connected){
					std::cout<<"当前与host链接的路由器"<<routers_timeout_list.at(i).router_ID<<"的断连时间是: "<<routers_timeout_list.at(i).expired_time.tv_sec<<"."<<routers_timeout_list.at(i).expired_time.tv_usec<<std::endl;
					//计算当前时间与3个T的超时时间的差
					time_difference = tv_one_mimus_two(current_time,routers_timeout_list.at(i).expired_time);
					//如果当前时间超过3T则断链接,设置距离为INF
					if(time_difference.tv_sec>=0||time_difference.tv_usec>=0){
						//设置这个路由器与当前host的链接为0
						routers_timeout_list.at(i).is_connected = 0;
						//设置距离为INF, 遍历当前host的路由表,找到这个超时的路由器
						for(int j=0;j<host_DV_table.size();j++){
							if(host_DV_table.at(j).dest_ID == routers_timeout_list.at(i).router_ID){
								//设置无穷
								host_DV_table.at(j).dest_COST = INF;
								host_DV_table.at(j).next_hop_ID = INF;
								break;
							}
						}
					}
				}
			}
            if(FD_ISSET(sock_index, &watch_list)){
				//控制器到路由器的链接
                /* control_socket */
                if(sock_index == control_socket){
					//创建一个TCP socket去处理控制器命令
					//一旦创建了这个socket之后就不用关闭了, 因为我会把这socket放到list里面,下次要用的时候再把这个socket调出来用就行了	
                    fdaccept = new_control_conn(sock_index);
					//将TCP socket放入list中
                    /* Add to watched socket list */
                    FD_SET(fdaccept, &master_list);
                    if(fdaccept > head_fd) head_fd = fdaccept;
                }
				//路由器到路由器关于更新路由表建立UDP的链接
                /* router_socket */
                else if(sock_index == router_socket){
                    //call handler that will call recvfrom() .....
					//如果是路由器socket,肯定是接受DV, 这里要通过这个socket,通过UDP的recvfrom读取路由表更新数据
					update_routing_table(sock_index);

                }
				//路由器到路由器关于传输数据建立TCP的链接
                /* data_socket */
                else if(sock_index == data_socket){
                    //new_data_conn(sock_index);
					//建立TCP链接,接受路由器之间的数据
					fdaccept = new_data_conn(sock_index);
					FD_SET(fdaccept, &master_list);
					if(fdaccept>head_fd) head_fd = fdaccept;
                }

                /* Existing connection */
                else{
					//如果在控制list中存在
                    if(isControl(sock_index)){
						//控制平面的处理
                        if(!control_recv_hook(sock_index)) FD_CLR(sock_index, &master_list);
                    }
					//如果在数据list中存在
                    else if (isData(sock_index)){
						//数据平面的处理
                        if (!data_recv_hook(sock_index)) FD_CLR(sock_index, &master_list);
					}
                    else ERROR("Unknown socket index");
                }
				if(next_event.tv_sec>0){
					gettimeofday(&current_event_no_timeout_happened,NULL);
					struct timeval diff_temp = tv_one_mimus_two(next_event,current_event_no_timeout_happened);
					if(diff_temp.tv_sec>=0 || diff_temp.tv_usec>=0){//没有发生超时
						waiting_time = diff_temp;
					}else{//发生超时了
						waiting_time.tv_usec = 0;
						waiting_time.tv_sec = 0;
					}
				}
            }
        }
    }
}
struct timeval tv_one_mimus_two(struct timeval tv1, struct timeval tv2) {
    struct timeval tv_diff;
    tv_diff.tv_sec = tv1.tv_sec - tv2.tv_sec;
    tv_diff.tv_usec = tv1.tv_usec - tv2.tv_usec;
    if (tv_diff.tv_usec < 0) {
        tv_diff.tv_sec--;
        tv_diff.tv_usec += 1000000;
    }
    return tv_diff;
}
void send_DISTANCE_VECTOR(){
	char *distance_vector, *payload;
	struct Route_Header *dv_header;
	//一开始的时候,有五个host需要去定时更新路由表
	int num_to_update = host_DV_table.size();
	//这个时候更新路由表,发送的是整个路由表过去
	//所以发送字节的数量是:routing header size + 5 routing content size
	int data_size = routing_header_size + num_to_update * routing_content_size;
	distance_vector = (char *)malloc(data_size);
	bzero(distance_vector, (size_t)data_size);
	//初始化header
	dv_header = (struct Route_Header *)(char *)malloc(sizeof(char) * routing_header_size);
	dv_header->num_update = htons((uint16_t)num_to_update);
	dv_header->source_ip = htons(current_router.my_ip);
	dv_header->source_port = htons(current_router.my_router_port);
	memcpy(distance_vector, dv_header,routing_header_size);
	//初始化payload
	payload=(char*)malloc(sizeof(char)*(num_to_update * routing_content_size));
	bzero(payload,(size_t)(num_to_update*routing_content_size));
	//将与当前host相链接的路由器的信息放入payload里面
	//信息存储在host_DV_table里面, 一共有五个路由器的信息
	int bit = 0;
	uint16_t table_router_port, table_router_id, table_router_cost;
	uint32_t table_router_ip;
	for(int i=0 ; i < num_to_update;i++){
		table_router_ip = htonl(host_DV_table.at(i).dest_IP);
		table_router_port = htons(host_DV_table.at(i).dest_router_PORT);
		table_router_id = htons(host_DV_table.at(i).dest_ID);
		table_router_cost = htons(host_DV_table.at(i).dest_COST);
		memcpy(payload+bit, &table_router_ip, sizeof(table_router_ip));
		bit+=4;
		memcpy(payload+bit,&table_router_port, sizeof(table_router_port));
		bit+=4;
		memcpy(payload+bit,&table_router_id, sizeof(table_router_id));
		bit+=2;
		memcpy(payload+=bit,&table_router_cost, sizeof(table_router_cost));
		bit+=2;
	}
	//之前的dv header已经占用了8位
	memcpy(distance_vector+routing_header_size, payload, (size_t)num_to_update*routing_content_size);
	//现在路由表的数据已经完全放入distance_vector里面了
	//因为可能周围的一些路由器发生了断连,所以不向所有路由器发送更新
	for(int j=0; j<neighbor_router_list.size(); j++){
		if(update_routing_table_UDP(neighbor_router_list.at(j).socket,distance_vector, neighbor_router_list.at(j).neighbor_router_IP, neighbor_router_list.at(j).neighbor_router_PORT, data_size)<0){
			std::cout<<"成功发送"<<std::endl;
		}
	}
}
//如果是路由器socket,肯定是接受DV, 这里要通过这个socket,通过UDP的recvfrom读取路由表更新数据
void update_routing_table(int sock_inde){
	
}

//UDP 常规操作
ssize_t update_routing_table_UDP(int sock_index, char* dv, uint32_t ip, uint16_t port, ssize_t length){
    ssize_t bytes = 0;
    struct sockaddr_in remote_router_addr;
    socklen_t addrlen = sizeof(remote_router_addr);
    bzero(&remote_router_addr, sizeof(remote_router_addr));
    remote_router_addr.sin_family = AF_INET;
    remote_router_addr.sin_addr.s_addr = htonl(ip);
    remote_router_addr.sin_port = htons(port);
    bytes = sendto(sock_index, dv, (size_t) length, 0, (struct sockaddr *) &remote_router_addr, addrlen);
    if (bytes == 0) return -1;
    return bytes;
}

//Sample code 常规操作Beej's socket那堆乱七八糟的
int create_control_sock()
{
    int sock;
    struct sockaddr_in control_addr;
    socklen_t addrlen = sizeof(control_addr);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
        ERROR("socket() failed");
    /* Make socket re-usable */
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int)) < 0)
        ERROR("setsockopt() failed");
    bzero(&control_addr, sizeof(control_addr));
    control_addr.sin_family = AF_INET;
    control_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    control_addr.sin_port = htons(CONTROL_PORT);
    if(bind(sock, (struct sockaddr *)&control_addr, sizeof(control_addr)) < 0)
        ERROR("bind() failed");
    if(listen(sock, 5) < 0)
        ERROR("listen() failed");
    return sock;
}
//创建控制链接,并且把控制socket放入list中
int new_control_conn(int sock_index) {
    int fdaccept, caddr_len;
    struct sockaddr_in remote_controller_addr;
    caddr_len = sizeof(remote_controller_addr);
    fdaccept = accept(sock_index, (struct sockaddr *) &remote_controller_addr, (socklen_t *) &caddr_len);
    if (fdaccept < 0) ERROR("accept() failed");
    control_socket_list.push_back(fdaccept);
    FD_SET(fdaccept, &master_list);
    if (fdaccept > head_fd) head_fd = fdaccept;
    return fdaccept;
}
//创建数据链接,并且把数据socket放入list中
int new_data_conn(int sock_index){
	    int fdaccept, caddr_len;
    struct sockaddr_in remote_data_addr;
    caddr_len = sizeof(remote_data_addr);
    fdaccept = accept(sock_index, (struct sockaddr *) &remote_data_addr, (socklen_t *) &caddr_len);
    if (fdaccept < 0) ERROR("accept() failed");
    struct DATA_SOCKET data_sock;
    data_sock.socket = fdaccept;
    data_sock.router_ip = ntohl(remote_data_addr.sin_addr.s_addr);
    data_socket_list.push_back(data_sock);
    FD_SET(fdaccept, &master_list);
    if (fdaccept > head_fd) head_fd = fdaccept;
    return fdaccept;
}
//判断这个sock_index 是否属于conotrol socket
bool isControl(int sock_index) {
    std::vector<int>::iterator it = find(control_socket_list.begin(), control_socket_list.end(), sock_index);
    return it != control_socket_list.end();
}

//判断这个sock_index 是否属于data socket
bool isData(int sock_index) {
	std::vector<DATA_SOCKET>::iterator it = find(data_socket_list.begin(), data_socket_list.end(),sock_index);
	return it != data_socket_list.end();
}


//TCP接收
ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = recv(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += recv(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}
//TCP发送
ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = send(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += send(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

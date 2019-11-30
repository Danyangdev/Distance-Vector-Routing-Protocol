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

#include "../include/global.h"
#include "../include/connection_manager.h"
#include <sys/select.h>
#include "../include/control_handler.h"
#include <iostream>
#include <sys/time.h>
#include <vector>
/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
//定义一个超时的数据结构
struct TIMEOUT{
	int router_ID;
	int is_connected;
	struct timeval expired_time;
};
//记录一个host上的与本机想链接的路由器们的超时3T的数据
std::vector<TIMEOUT> routers_timeout_list;
//定义DV本身
struct DV
{
	uint16_t dest_ID;
	uint16_t cost;
};

//定义与当前host相链接的路由器的信息
struct ROUTER_INFO_ON_TABLE{
	uint16_t dest_ID;
	uint16_t dest_router_PORT;
	uint16_t dest_data_PORT;
	uint16_t dest_COST;
	uint32_t dest_IP;
	uint16_t next_hop_ID;
	std::vector<DV> dv_LIST;
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
int main(int argc, char **argv)
{
    /*Start Here*/
    sscanf(argv[1], "%hu", &CONTROL_PORT);
    init(); // Initialize connection manager; This will block
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
				//send_DV();
				//gettimeofday(&current_time,NULL);
				std::cout<<"发送玩DV的时刻:　"<<current_time.tv_sec<<"."<<current_time.tv_usec/100000<<"s"<<std::endl;
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
		}
        /* Loop through file descriptors to check which ones are ready */
		//看哪个fd准备去干事儿
        for(sock_index=0; sock_index<=head_fd; sock_index+=1){

            if(FD_ISSET(sock_index, &watch_list)){
				//控制器到路由器的链接
                /* control_socket */
                if(sock_index == control_socket){
					//创建一个TCP socket去处理控制器命令
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
					//update_routing_table(sock_index);

                }
				//路由器到路由器关于传输数据建立TCP的链接
                /* data_socket */
                else if(sock_index == data_socket){
                    //new_data_conn(sock_index);
					//建立TCP链接,接受路由器之间的数据
					//fdaccept = new_data_conn(sock_index);
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
                    //else if (isData(sock_index)){
						//数据平面的处理
                    //    if (!data_recv_hook(sock_index)) FD_CLR(sock_index, &master_list);
					//}
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


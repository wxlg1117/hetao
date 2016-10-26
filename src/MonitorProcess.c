/*
 * hetao - High Performance Web Server
 * author	: calvin
 * email	: calvinwilliams@163.com
 *
 * Licensed under the LGPL v2.1, see the file LICENSE in base directory.
 */

#include "hetao_in.h"

#if ( defined __linux ) || ( defined __unix )

static sig_atomic_t		g_SIGUSR1_flag = 0 ;
static sig_atomic_t		g_SIGUSR2_flag = 0 ;
static sig_atomic_t		g_SIGTERM_flag = 0 ;
static signed char		g_monitor_exit_flag = 0 ;

static void sig_set_flag( int sig_no )
{
	UPDATEDATETIMECACHEFIRST
	/* InfoLog( __FILE__ , __LINE__ , "recv signal[%d]" , sig_no ); */
	
	/* 信号回调函数，只是设置标志变量 */
	if( sig_no == SIGUSR1 )
	{
		g_SIGUSR1_flag = 1 ;
	}
	else if( sig_no == SIGUSR2 )
	{
		g_SIGUSR2_flag = 1 ;
	}
	else if( sig_no == SIGTERM )
	{
		g_SIGTERM_flag = 1 ;
	}
	
	return;
}

static void sig_proc( struct HetaoEnv *p_env )
{
	int		i ;
	
	int		nret = 0 ;
	
	UPDATEDATETIMECACHEFIRST
	
	/* 处理标志变量 */
	if( g_SIGUSR1_flag == 1 )
	{
		/* 通知所有工作进程重新打开事件日志 */
		for( i = 0 ; i < g_p_env->worker_processes ; i++ )
		{
			char	ch = SIGNAL_REOPEN_LOG ;
			nret = write( g_p_env->process_info_array[i].pipe[1] , & ch , 1 ) ;
		}
		
		g_SIGUSR1_flag = 0 ;
	}
	else if( g_SIGUSR2_flag == 1 )
	{
		pid_t			pid ;
		
		int			nret = 0 ;
		
		/* 保存侦听信息到环境变量中 */
		nret = SaveListenSockets( g_p_env ) ;
		if( nret )
		{
			ErrorLog( __FILE__ , __LINE__ , "SaveListenSockets faild[%d]" , nret );
			return;
		}
		
		/* 保存老日志文件名 */
		setenv( HETAO_LOG_PATHFILENAME , g_log_pathfilename , 1 );
		
		/* 产生下一辈进程 */
		pid = fork() ;
		if( pid == -1 )
		{
			;
		}
		else if( pid == 0 )
		{
			InfoLog( __FILE__ , __LINE__ , "execvp ..." );
			execvp( "hetao" , g_p_env->argv );
			FatalLog( __FILE__ , __LINE__ , "execvp failed , errno[%d]" , ERRNO );
			
			exit(9);
		}
		
		g_SIGUSR2_flag = 0 ;
	}
	else if( g_SIGTERM_flag == 1 )
	{
		/* 以关闭通知管道的方式通知所有工作进程优雅结束 */
		for( i = 0 ; i < g_p_env->worker_processes ; i++ )
		{
			DebugLog( __FILE__ , __LINE__ , "Close pipe[%d]" , g_p_env->process_info_array[i].pipe[1] );
			close( g_p_env->process_info_array[i].pipe[1] );
		}
		
		g_SIGTERM_flag = 0 ;
		g_monitor_exit_flag = 1 ;
	}
	
	return;
}

#elif ( defined _WIN32 )
#endif

#if ( defined __linux ) || ( defined __unix )

int MonitorProcess( void *pv )
{
	struct HetaoEnv	*p_env = (struct HetaoEnv *)pv ;
	
	int			i , j ;
	int			worker_processes ;
	
	struct sigaction	act ;
	
	pid_t			pid ;
	int			status ;
	
	int			nret = 0 ;
	
	SETPID
	SETTID
	UPDATEDATETIMECACHEFIRST
	InfoLog( __FILE__ , __LINE__ , "--- master begin ---" );
	
	/* 只响应信号TERM、USR1、USR2 */
	/*
		TERM : 结束
		USR1 : 重新打开事件日志
		USR2 : 产生下一辈进程，用于优雅重启
	*/
	act.sa_handler = & sig_set_flag ;
	sigemptyset( & (act.sa_mask) );
	act.sa_flags = 0 ;
	signal( SIGCLD , SIG_DFL );
	signal( SIGCHLD , SIG_DFL );
	signal( SIGPIPE , SIG_IGN );
	sigaction( SIGTERM , & act , NULL );
	sigaction( SIGUSR1 , & act , NULL );
	sigaction( SIGUSR2 , & act , NULL );
	
	/* 创建所有工作进程组 */
	for( i = 0 ; i < p_env->worker_processes ; i++ )
	{
		/* 创建命令管道 */
		nret = pipe( p_env->process_info_array[i].pipe ) ;
		if( nret )
		{
			ErrorLog( __FILE__ , __LINE__ , "pipe failed , errno[%d]" , ERRNO );
			return -1;
		}
		SetHttpCloseExec( p_env->process_info_array[i].pipe[0] );
		SetHttpCloseExec( p_env->process_info_array[i].pipe[1] );
		DebugLog( __FILE__ , __LINE__ , "create pipe #%d# #%d#" , p_env->process_info_array[i].pipe[0] , p_env->process_info_array[i].pipe[1] );
		
		/* 创建工作进程 */
		p_env->p_this_process_info = p_env->process_info_array + i ;
		p_env->process_info_index = i ;
		
		pid = fork() ;
		UPDATEDATETIMECACHEFIRST
		if( pid == -1 )
		{
			ErrorLog( __FILE__ , __LINE__ , "fork failed , errno[%d]" , ERRNO );
			return -1;
		}
		else if( pid == 0 )
		{
			SETPID
			SETTID
			UPDATEDATETIMECACHE
			
			InfoLog( __FILE__ , __LINE__ , "child : [%ld] fork [%ld]" , getppid() , getpid() );
			
			close( p_env->process_info_array[i].pipe[1] ) ;
			DebugLog( __FILE__ , __LINE__ , "pipe #%d# close #%d#" , p_env->process_info_array[i].pipe[0] , p_env->process_info_array[i].pipe[1] );
			for( j = i - 1 ; j >= 0 ; j-- )
			{
				close( p_env->process_info_array[j].pipe[1] ) ;
				DebugLog( __FILE__ , __LINE__ , "pipe close #%d#" , p_env->process_info_array[j].pipe[1] );
			}
			
			nret = WorkerProcess((void*)p_env) ;
			
			return -nret;
		}
		else
		{
			p_env->process_info_array[i].pid = pid ;
			
			InfoLog( __FILE__ , __LINE__ , "parent : [%ld] fork [%ld]" , getpid() , p_env->process_info_array[i].pid );
			close( p_env->process_info_array[i].pipe[0] ) ;
			DebugLog( __FILE__ , __LINE__ , "pipe #%d# close #%d#" , p_env->process_info_array[i].pipe[1] , p_env->process_info_array[i].pipe[0] );
		}
	}
	
	/* 监控所有工作进程。如果发现有结束，重启它 */
	worker_processes = p_env->worker_processes ;
	while( worker_processes > 0 )
	{
_WAITPID :
		pid = waitpid( -1 , & status , 0 );
		UPDATEDATETIMECACHEFIRST
		if( pid == -1 )
		{
			if( ERRNO == EINTR )
			{
				sig_proc( p_env );
				goto _WAITPID;
			}
			
			ErrorLog( __FILE__ , __LINE__ , "waitpid failed , errno[%d]" , ERRNO );
			return -1;
		}
		
		if( WEXITSTATUS(status) == 0 && WIFSIGNALED(status) == 0 && WTERMSIG(status) == 0 )
		{
			InfoLog( __FILE__ , __LINE__
				, "waitpid[%ld] WEXITSTATUS[%d] WIFSIGNALED[%d] WTERMSIG[%d]"
				, pid , WEXITSTATUS(status) , WIFSIGNALED(status) , WTERMSIG(status) );
		}
		else
		{
			ErrorLog( __FILE__ , __LINE__
				, "waitpid[%ld] WEXITSTATUS[%d] WIFSIGNALED[%d] WTERMSIG[%d]"
				, pid , WEXITSTATUS(status) , WIFSIGNALED(status) , WTERMSIG(status) );
		}
		
		for( i = 0 ; i < p_env->worker_processes ; i++ )
		{
			if( p_env->process_info_array[i].pid == pid )
				break;
		}
		if( i >= p_env->worker_processes )
			goto _WAITPID;
		
		close( p_env->process_info_array[i].pipe[1] );
		
		worker_processes--;
		InfoLog( __FILE__ , __LINE__ , "worker_processes[%d]" , worker_processes );
		
		if( g_monitor_exit_flag == 1 )
			continue;
		
		sleep(1);
		
		/* 创建管道 */
		nret = pipe( p_env->process_info_array[i].pipe ) ;
		if( nret )
		{
			ErrorLog( __FILE__ , __LINE__ , "pipe failed , errno[%d]" , ERRNO );
			return -1;
		}
		SetHttpCloseExec( p_env->process_info_array[i].pipe[0] );
		SetHttpCloseExec( p_env->process_info_array[i].pipe[1] );
		DebugLog( __FILE__ , __LINE__ , "Create pipe #%d# #%d#" , p_env->process_info_array[i].pipe[0] , p_env->process_info_array[i].pipe[1] );
		
		/* 创建工作进程 */
		p_env->p_this_process_info = p_env->process_info_array + i ;
		p_env->process_info_index = i ;
		
_FORK :
		p_env->process_info_array[i].pid = fork() ;
		UPDATEDATETIMECACHEFIRST
		if( p_env->process_info_array[i].pid == -1 )
		{
			if( ERRNO == EINTR )
			{
				sig_proc( p_env );
				goto _FORK;
			}
			
			ErrorLog( __FILE__ , __LINE__ , "fork failed , errno[%d]" , ERRNO );
			return -1;
		}
		else if( p_env->process_info_array[i].pid == 0 )
		{
			SETPID
			SETTID
			UPDATEDATETIMECACHE
			
			InfoLog( __FILE__ , __LINE__ , "child : [%ld] fork [%ld]" , getppid() , getpid() );
			
			close( p_env->process_info_array[i].pipe[1] ) ;
			DebugLog( __FILE__ , __LINE__ , "pipe #%d# close #%d#" , p_env->process_info_array[i].pipe[0] , p_env->process_info_array[i].pipe[1] );
			for( j = p_env->worker_processes-1 ; j >= 0 ; j-- )
			{
				if( j != i )
				{
					close( p_env->process_info_array[j].pipe[1] ) ;
					DebugLog( __FILE__ , __LINE__ , "pipe close #%d#" , p_env->process_info_array[j].pipe[1] );
				}
			}
			
			nret = WorkerProcess((void*)p_env) ;
			
			return -nret;
		}
		else
		{
			InfoLog( __FILE__ , __LINE__ , "parent : [%ld] fork [%ld]" , getpid() , p_env->process_info_array[i].pid );
			close( p_env->process_info_array[i].pipe[0] ) ;
			DebugLog( __FILE__ , __LINE__ , "pipe #%d# close #%d#" , p_env->process_info_array[i].pipe[1] , p_env->process_info_array[i].pipe[0] );
		}
		
		worker_processes++;
		InfoLog( __FILE__ , __LINE__ , "worker_processes[%d]" , worker_processes );
	}
	
	CleanEnvirment( p_env );
	
	InfoLog( __FILE__ , __LINE__ , "--- master exit ---" );
	
	return 0;
}

#elif ( defined _WIN32 )

int MonitorProcess( void *pv )
{
	struct HetaoEnv		*p_env = (struct HetaoEnv *)pv ;
	
	/*
	HANDLE			hReadPipe ;
	HANDLE			hWritePipe ;
	SECURITY_ATTRIBUTES	sa ;
	*/
	char			env_value[ 64 + 1 ] ;
	
	char			szCommandLine[ MAX_PATH + 1 ] ;
	struct ListenSession	*p_listen_session = NULL ;
	
	BOOL			bret ;
	DWORD			dwret ;
	int			nret = 0 ;
	
	SETPID
	SETTID
	UPDATEDATETIMECACHEFIRST
	InfoLog( __FILE__ , __LINE__ , "--- master begin ---" );
	
	SaveListenSockets( p_env );
	
	memset( env_value , 0x00 , sizeof(env_value) );
	SNPRINTF( env_value , sizeof(env_value)-1 , "%s=%d" , HETAO_PROCESS_INFO , p_env->process_info_shmid );
	DebugLog( __FILE__ , __LINE__ , "putenv[%s]" , env_value );
	PUTENV( env_value );
	
	p_env->p_process_info->dwParentProcessId = GetCurrentProcessId() ;
	InfoLog( __FILE__ , __LINE__ , "GetCurrentProcessId()[%d]" , p_env->p_process_info->dwParentProcessId );
	
	/* 创建命令管道 */
	/*
	memset( & sa , 0x00 , sizeof(SECURITY_ATTRIBUTES) );
	sa.nLength = sizeof(SECURITY_ATTRIBUTES) ;
	sa.bInheritHandle = TRUE ;
	bret = CreatePipe( & hReadPipe , & hWritePipe , & sa , 0 ) ;
	if( bret != TRUE )
	{
		ErrorLog( __FILE__ , __LINE__ , "CreatePipe failed , errno[%d]" , ERRNO );
		return -1;
	}
	DebugLog( __FILE__ , __LINE__ , "CreatePipe #%d# #%d#" , hReadPipe , hWritePipe );
	
	memset( env_value , 0x00 , sizeof(env_value) );
	SNPRINTF( env_value , sizeof(env_value)-1 , "%s=%d" , HETAO_READ_PIPE , hReadPipe );
	DebugLog( __FILE__ , __LINE__ , "putenv[%s]" , env_value );
	PUTENV( env_value );
	memset( env_value , 0x00 , sizeof(env_value) );
	SNPRINTF( env_value , sizeof(env_value)-1 , "%s=%d" , HETAO_WRITE_PIPE , hWritePipe );
	DebugLog( __FILE__ , __LINE__ , "putenv[%s]" , env_value );
	PUTENV( env_value );
	*/
	
	/* 创建进程  */
	memset( szCommandLine , 0x00 , sizeof(szCommandLine) );
	SNPRINTF( szCommandLine , sizeof(szCommandLine)-1 , "\"%s\" \"%s\" --child" , p_env->argv[0] , p_env->argv[1] );
	bret = CreateProcess( NULL , szCommandLine , NULL , NULL , TRUE , 0 , NULL , NULL , & (p_env->p_process_info->si) , & (p_env->p_process_info->pi) ) ;
	if( bret == FALSE )
	{
		ErrorLog( __FILE__ , __LINE__ , "CreateProcess[%s] failed , errno[%d]" , szCommandLine , ERRNO );
		return -1;
	}
	else
	{
		InfoLog( __FILE__ , __LINE__ , "CreateProcess[%s] ok" , szCommandLine );
		//CloseHandle( hReadPipe );
	}
	
	/*
	memset( env_value , 0x00 , sizeof(env_value) );
	SNPRINTF( env_value , sizeof(env_value)-1 , "%s=%d" , HETAO_READ_PIPE , hReadPipe );
	*/
	while(1)
	{
		/* 监控子进程结束 */
		dwret = WaitForSingleObject( p_env->p_process_info->pi.hProcess , INFINITE ) ;
		if( dwret == WAIT_FAILED )
		{
			ErrorLog( __FILE__ , __LINE__ , "WaitForSingleObject failed , errno[%d]" , ERRNO );
			return -1;
		}
		else
		{
			InfoLog( __FILE__ , __LINE__ , "WaitForSingleObject ok" );
		}
		
		// CloseHandle( hWritePipe );
		
		CloseHandle( p_env->p_process_info->pi.hProcess );
		CloseHandle( p_env->p_process_info->pi.hThread );
		
		/* 重建侦听 */
		list_for_each_entry( p_listen_session , & (p_env->listen_session_list.list) , struct ListenSession , list )
		{
			CLOSESOCKET( p_listen_session->netaddr.sock );
			
			p_listen_session->netaddr.sock = WSASocket( AF_INET , SOCK_STREAM , 0 , NULL , 0 , WSA_FLAG_OVERLAPPED ) ;
			if( p_listen_session->netaddr.sock == -1 )
			{
				ErrorLog( __FILE__ , __LINE__ , "socket failed , errno[%d]" , ERRNO );
				return -1;
			}
			
			SetHttpReuseAddr( p_listen_session->netaddr.sock );
			
			nret = bind( p_listen_session->netaddr.sock , (struct sockaddr *) & (p_listen_session->netaddr.addr) , sizeof(struct sockaddr) ) ;
			if( nret == -1 )
			{
				ErrorLog( __FILE__ , __LINE__ , "bind[%s:%d] failed , errno[%d]" , p_listen_session->netaddr.ip , p_listen_session->netaddr.port , ERRNO );
				return -1;
			}
			
			nret = listen( p_listen_session->netaddr.sock , 10240 ) ;
			if( nret == -1 )
			{
				ErrorLog( __FILE__ , __LINE__ , "listen failed , errno[%d]" , ERRNO );
				return -1;
			}
			
			DebugLog( __FILE__ , __LINE__ , "[%s:%d] listen #%d#" , p_listen_session->netaddr.ip , p_listen_session->netaddr.port , p_listen_session->netaddr.sock );
		}
		
		/* 创建命令管道 */
		/*
		memset( & sa , 0x00 , sizeof(SECURITY_ATTRIBUTES) );
		sa.nLength = sizeof(SECURITY_ATTRIBUTES) ;
		sa.bInheritHandle = TRUE ;
		bret = CreatePipe( & hReadPipe , & hWritePipe , & sa , 0 ) ;
		if( bret != TRUE )
		{
			ErrorLog( __FILE__ , __LINE__ , "CreatePipe failed , errno[%d]" , ERRNO );
			return -1;
		}
		DebugLog( __FILE__ , __LINE__ , "CreatePipe #%d# #%d#" , hReadPipe , hWritePipe );
		
		memset( env_value , 0x00 , sizeof(env_value) );
		SNPRINTF( env_value , sizeof(env_value)-1 , "%s=%d" , HETAO_READ_PIPE , hReadPipe );
		DebugLog( __FILE__ , __LINE__ , "putenv[%s]" , env_value );
		PUTENV( env_value );
		memset( env_value , 0x00 , sizeof(env_value) );
		SNPRINTF( env_value , sizeof(env_value)-1 , "%s=%d" , HETAO_WRITE_PIPE , hWritePipe );
		DebugLog( __FILE__ , __LINE__ , "putenv[%s]" , env_value );
		PUTENV( env_value );
		*/
		
		/* 创建进程  */
		memset( szCommandLine , 0x00 , sizeof(szCommandLine) );
		SNPRINTF( szCommandLine , sizeof(szCommandLine)-1 , "\"%s\" \"%s\" --child" , p_env->argv[0] , p_env->argv[1] );
		bret = CreateProcess( NULL , szCommandLine , NULL , NULL , TRUE , 0 , NULL , NULL , & (p_env->p_process_info->si) , & (p_env->p_process_info->pi) ) ;
		if( bret == FALSE )
		{
			ErrorLog( __FILE__ , __LINE__ , "CreateProcess[%s] failed , errno[%d]" , szCommandLine , ERRNO );
			return -1;
		}
		else
		{
			InfoLog( __FILE__ , __LINE__ , "CreateProcess[%s] ok" , szCommandLine );
			//CloseHandle( hReadPipe );
		}
		
		Sleep( 1000 );
	}
	
	return 0;
}

#endif

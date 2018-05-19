#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>

#include <mysql/mysql.h>

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <time.h>
#include <math.h>

#define CS_MCP3208  8        // BCM_GPIO_8

#define SPI_CHANNEL 0
#define SPI_SPEED   1000000   // 1MHz

#define VCC         4.8       // Supply Voltage

// define farm 
#define MAXTIMINGS 85
#define RETRY 5

#define LIGHTSEN_OUT 2  //gpio27 - J13 connect
int ret_humid, ret_temp;

static int DHTPIN = 7;
static int dht22_dat[5] = {0,0,0,0,0};

int read_dht22_dat_temp();

// here
#define MAX 100000
#define DBHOST "localhost"
#define DBUSER "root"
#define DBPASS "ekqlcl7229"
#define DBNAME "demofarmdb"

MYSQL *connector;
MYSQL_RES *result;
MYSQL_ROW row;

int buffer[MAX];
int fill_ptr = 0;
int use_ptr = 0;
int count = 0;

void put(int value)
{
	buffer[fill_ptr] = value;
	printf("%d\n", buffer[fill_ptr]);
	fill_ptr = (fill_ptr + 1) % MAX;
	count++;
}
int get()
{
	int tmp = buffer[use_ptr];
	use_ptr = (use_ptr + 1 ) %MAX;
	count--;
	printf("%d\n", tmp);
	return tmp;
}


pthread_cond_t pro,con;
pthread_mutex_t mutex;

void *producer(void *arg)
{
	int i;
	int TempValue;
	for(i=0; i <MAX; i++)
	{
		delay(3000);
		pthread_mutex_lock(&mutex);
		while(count == MAX)
			pthread_cond_wait(&pro, &mutex);
		//delay(3000);
		TempValue = read_dht22_dat_temp();
		put(TempValue);
		//delay(3000);
		pthread_cond_signal(&con);
		pthread_mutex_unlock(&mutex);
	}

}

void *consumer(void *arg)
{
	int i;
	for(i = 0 ; i< MAX; i++)
	{
		pthread_mutex_lock(&mutex);
		while(count == 0)
			pthread_cond_wait(&con, &mutex);

		int tmp = get();
		delay(100);
		connector = mysql_init(NULL);
		if(!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0))
		{
			fprintf(stderr, "%s\n", mysql_error(connector));
			return 0;
		}
		printf("mysql opened\n");
		char query[1024];
		sprintf(query, "insert into ProContemp values (now(), %d)",tmp);
		if(mysql_query(connector, query))
		{
			fprintf(stderr, "%s\n", mysql_error(connector));
			printf("Write DB error \n");
		}

		pthread_cond_signal(&pro);
		pthread_mutex_unlock(&mutex);
	}
}


int main (void)
{
 
  if(wiringPiSetupGpio() == -1)
  {
    fprintf (stdout, "Unable to start wiringPi: %s\n", strerror(errno));
    return 1 ;
  }

  if(wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1)
  {
    fprintf (stdout, "wiringPiSPISetup Failed: %s\n", strerror(errno));
    return 1 ;
  }

    // MySQL connection
  connector = mysql_init(NULL);
  if (!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0))
  {
    fprintf(stderr, "%s\n", mysql_error(connector));
    return 0;
  }

  printf("MySQL(rpidb) opened.\n");
  pthread_t produce, consumers;

  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&pro, NULL);
  pthread_cond_init(&con, NULL);
  pthread_create(&produce, NULL, producer, NULL);
  pthread_create(&consumers, NULL, consumer, NULL);

  pthread_join(produce, NULL);
  pthread_join(consumers, NULL);

  mysql_close(connector);
  pthread_cond_destroy(&pro);
  pthread_cond_destroy(&con);
  
  return 0;
}

static uint8_t sizecvt(const int read)
{
  /* digitalRead() and friends from wiringpi are defined as returning a value
  < 256. However, they are returned as int() types. This is a safety function */

  if (read > 255 || read < 0)
  {
    printf("Invalid data from wiringPi library\n");
    exit(EXIT_FAILURE);
  }
  return (uint8_t)read;
}
int read_dht22_dat_temp()
{
  uint8_t laststate = HIGH;
  uint8_t counter = 0;
  uint8_t j = 0, i;

  dht22_dat[0] = dht22_dat[1] = dht22_dat[2] = dht22_dat[3] = dht22_dat[4] = 0;

  // pull pin down for 18 milliseconds
  pinMode(DHTPIN, OUTPUT);
  digitalWrite(DHTPIN, HIGH);
  delay(10);
  digitalWrite(DHTPIN, LOW);
  delay(18);
  // then pull it up for 40 microseconds
  digitalWrite(DHTPIN, HIGH);
  delayMicroseconds(40); 
  // prepar
  
  pinMode(DHTPIN, INPUT);

  // detect change and read data
  for ( i=0; i< MAXTIMINGS; i++) {
    counter = 0;
    while (sizecvt(digitalRead(DHTPIN)) == laststate) {
      counter++;
      delayMicroseconds(1);
      if (counter == 255) {
        break;
      }
    }
    laststate = sizecvt(digitalRead(DHTPIN));

    if (counter == 255) break;

    // ignore first 3 transitions
    if ((i >= 4) && (i%2 == 0)) {
      // shove each bit into the storage bytes
      dht22_dat[j/8] <<= 1;
      if (counter > 50)
        dht22_dat[j/8] |= 1;
      j++;
    }
  }

  // check we read 40 bits (8bit x 5 ) + verify checksum in the last byte
  // print it out if data is good
  if ((j >= 40) && 
      (dht22_dat[4] == ((dht22_dat[0] + dht22_dat[1] + dht22_dat[2] + dht22_dat[3]) & 0xFF)) ) {
        float t, h;
		
        h = (float)dht22_dat[0] * 256 + (float)dht22_dat[1];
        h /= 10;
        t = (float)(dht22_dat[2] & 0x7F)* 256 + (float)dht22_dat[3];
        t /= 10.0;
        if ((dht22_dat[2] & 0x80) != 0)  t *= -1;
		
		ret_humid = (int)h;
		ret_temp = (int)t;
		printf("Temperature = %d\n", ret_temp);
		
    return ret_temp;
  }
  else
  {
    printf("Data not good, skip\n");
    return 0;
  }
}

int wiringPicheck(void)
{
	if (wiringPiSetup () == -1)
	{
		fprintf(stdout, "Unable to start wiringPi: %s\n", strerror(errno));
		return 1 ;
	}
}


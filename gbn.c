/*
 * John Ying (U15144564)
 * GRS CS655
 * Programming Assignment 2
 *
 * Go-Back-N
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <netinet/in.h>

/* ******************************************************************
   ARQ NETWORK EMULATOR: VERSION 1.1  J.F.Kurose
   MODIFIED by Chong Wang on Oct.21,2005 for csa2,csa3 environments

   This code should be used for PA2, unidirectional data transfer protocols 
   (from A to B)
   Network properties:
   - one way network delay averages five time units (longer if there
     are other messages in the channel for Pipelined ARQ), but can be larger
   - packets can be corrupted (either the header or the data portion)
     or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
     (although some can be lost).
**********************************************************************/

/* a "msg" is the data unit passed from layer 5 (teachers code) to layer  */
/* 4 (students' code).  It contains the data (characters) to be delivered */
/* to layer 5 via the students transport level protocol entities.         */
typedef struct msg {
    char data[20];
} msg;

/* a packet is the data unit passed from layer 4 (students code) to layer */
/* 3 (teachers code).  Note the pre-defined packet structure, which all   */
/* students must follow. */
typedef struct pkt {
    int seqnum;
    int acknum;
    int checksum;
    char payload[20];
} pkt;

#define     A               0
#define     B               1
#define     FIRST_SEQNUM    0
#define     FALSE           0
#define     TRUE            1

void restart_rxmt_timer(void);
void toLayer3(int AorB, struct pkt packet);
void toLayer5(char datasent[20]);
void startTimer(int AorB, double increment);
void stopTimer(int AorB);

/* WINDOW_SIZE, RXMT_TIMEOUT and TRACE are inputs to the program;
   We have set an appropriate value for LIMIT_SEQNUM.
   You do not have to concern but you have to use these variables in your 
   routines */

extern int WINDOW_SIZE;         // size of the window
extern int LIMIT_SEQNUM;        // maximum sequence number for GBN
extern double RXMT_TIMEOUT;     // retransmission timeout
extern int TRACE;               // trace level, for your debug purpose
extern double time_now;         // simulation time, for your debug purpose
extern int nsimmax;             // maximum number of messages sent
extern double lossProb;         // loss probability
extern double corruptProb;      // corruption probability

// Entity A
int txBase;                     // first sequence number of window
int nextSeqNum;                 // last sequence number within window
int nextMsg;                    // message index
msg *msgBuffer;                 // message buffer
pkt *txPktBuffer;               // packet buffer
int msgCount;                   // message count

// Entity B
int expectSeqNum;               // expected sequence number
int lastAckNum;                 // last acknowledgement number

// Statistics
int txPktCount;                 // count of packets sent by A
int rxPktCount;                 // count of packets received by B
int corruptPktCount;            // count of corrupted packets received by B
int rxmtCount;                  // retransmit count
double *txPktTime;              // transmit time of each packet
double totalTtc;                // total time to communicate
int commPktCount;               // count of communicated packets
int receivedMsgCount;           // count of messages received by B
double *msgArriveTime;          // arrival times of each message
double totalDelay;              // total queuing delay

// Print window
void printWindow(int base)
{
    int i, end = (base + WINDOW_SIZE) % LIMIT_SEQNUM;

    printf("    WINDOW: [");
    for (i = base; i != end; i = (i + 1) % LIMIT_SEQNUM)
        printf(" %d", i);
    printf(" ]\n");
}

// Check if number is within window
int isWithinWindow(int base, int i)
{
    // Number located right of base
    int right = i >= base && i < base + WINDOW_SIZE;

    // Number located left of base
    int left = i < base && i + LIMIT_SEQNUM < base + WINDOW_SIZE;
    
    return right || left;
}

// Calculate checksum of packet
int calcChecksum(pkt packet)
{
    int i, checksum = 0;

    checksum += packet.seqnum;
    checksum += packet.acknum;

    for (i = 0; i < 20; i++)
        checksum += packet.payload[i];

    return checksum;
}

// Check packet for errors
int isCorrupt(pkt packet)
{
    return calcChecksum(packet) - packet.checksum;
}

// Called from layer 5, pass the data to be sent to other side
void A_output(msg message)
{
    printf("  A: Receiving MSG from above...\n");
    printf("    DATA: %.*s\n", 19, message.data);
    printWindow(txBase);

    // Record message arrival time
    msgArriveTime[msgCount] = time_now;

    // Add message to buffer and update message count
    msgBuffer[msgCount] = message;
    msgCount++;

    // Next sequence number is within window
    if (isWithinWindow(txBase, nextSeqNum))
    {
        pkt new_packet;

        // Create DATA packet
        new_packet.seqnum = nextSeqNum;
        new_packet.acknum = 0;
        memcpy(new_packet.payload, msgBuffer[nextMsg].data, sizeof(msgBuffer[nextMsg].data));
        new_packet.checksum = calcChecksum(new_packet);

        // Add to packet buffer
        txPktBuffer[nextSeqNum] = new_packet;

        // Send packet to network
        printf("  A: Sending new DATA to B...\n");
        printf("    SEQ, ACK: %d, %d\n", new_packet.seqnum, new_packet.acknum);
        printf("    CHECKSUM: %d\n", new_packet.checksum);
        printf("    PAYLOAD: %.*s\n", 19, new_packet.payload);
        printWindow(txBase);
        toLayer3(A, new_packet);

        // Set timer if packet is first in window
        if (nextSeqNum == txBase)
            startTimer(A, RXMT_TIMEOUT);

        // Record transmit time and count
        txPktTime[nextSeqNum] = time_now;
        txPktTime++;

        // Update next sequence number
        nextSeqNum = (nextSeqNum + 1) % LIMIT_SEQNUM;

        // Update next message index
        nextMsg++;
    }
}

// Called from layer 3, when a packet arrives for layer 4 
void A_input(pkt packet)
{
    printf("  A: Receiving ACK from B...\n");
    printf("    SEQ, ACK: %d, %d\n", packet.seqnum, packet.acknum);
    printf("    CHECKSUM: %d\n", packet.checksum);
    printf("    PAYLOAD: %.*s\n", 19, packet.payload);
    printWindow(txBase);

    // No errors and ACK number within window
    if (!isCorrupt(packet) && isWithinWindow(txBase, packet.acknum))
    {
        int i, shift;

        printf("  A: Accepting ACK from B...\n");
        printf("*Tx Time: %f\n", time_now - txPktTime[packet.acknum]);
        printf("    Time: %f\n", txPktTime[packet.acknum]);
        printWindow(txBase);

        // Record time to communicate
        totalTtc += time_now - txPktTime[packet.acknum];
        commPktCount++;

        // Stop timer
        stopTimer(A);

        // Find number of times window shifted
        if (packet.acknum < txBase)
            shift = packet.acknum - txBase + LIMIT_SEQNUM;
        else
            shift = packet.acknum - txBase;

        // Update base
        txBase = (packet.acknum + 1) % LIMIT_SEQNUM;

        // Iterate through newly available slots
        for (i = 0; i < shift + 1; i++)
        {
            // Outstanding messages available
            if (nextMsg < msgCount)
            {
                // Record total queuing delay
                totalDelay += time_now - msgArriveTime[nextMsg];

                // Create DATA packet
                pkt new_packet;
                new_packet.seqnum = nextSeqNum;
                new_packet.acknum = 0;
                memcpy(new_packet.payload, msgBuffer[nextMsg].data, sizeof(msgBuffer[nextMsg].data));
                new_packet.checksum = calcChecksum(new_packet);

                // Add to packet buffer
                txPktBuffer[nextSeqNum] = new_packet;

                // Send packet to network
                printf("  A: Sending new DATA to B...\n");
                printf("    SEQ, ACK: %d, %d\n", new_packet.seqnum, new_packet.acknum);
                printf("    CHECKSUM: %d\n", new_packet.checksum);
                printf("    PAYLOAD: %.*s\n", 19, new_packet.payload);
                printWindow(txBase);
                toLayer3(A, new_packet);

                // Record transmit time and count
                txPktTime[nextSeqNum] = time_now;
                txPktCount++;

                // Update next sequence number
                nextSeqNum = (nextSeqNum + 1) % LIMIT_SEQNUM;

                // Update next message index
                nextMsg++;
            }
        }

        // Set timer if there are still packets to send
        if (txBase != nextSeqNum)
            startTimer(A, RXMT_TIMEOUT);
    }
    else
    {
        // Discard packet
        printf("  A: Rejecting ACK from B... (pending ACK %d)\n", txBase);
        printWindow(txBase);
    }
}

// Called when A's timer goes off
void A_timerInterrupt(void)
{
    pkt new_packet;
    int i = txBase;

    // Iterate through window
    while (i != nextSeqNum)
    {
        // Resend packet to network
        new_packet = txPktBuffer[i];
        printf("  A: Resending DATA to B...\n");
        printf("    SEQ, ACK: %d, %d\n", new_packet.seqnum, new_packet.acknum);
        printf("    CHECKSUM: %d\n", new_packet.checksum);
        printf("    PAYLOAD: %.*s\n", 19, new_packet.payload);
        printWindow(txBase);
        toLayer3(A, new_packet);

        // Update transmit and retransmit counts
        txPktCount++;
        rxmtCount++;

        // Iterate
        i = (i + 1) % LIMIT_SEQNUM;
    }

    // Set timer
    startTimer(A, RXMT_TIMEOUT);
} 

// Called once before any other entity A routines are called
void A_init(void)
{
    // Allocate buffers
    nextMsg = 0;
    msgCount = 0;
    msgBuffer = (msg *) malloc(sizeof(msg) * nsimmax);
    txPktBuffer = (pkt *) malloc(sizeof(pkt) * LIMIT_SEQNUM);

    // State variables
    txBase = FIRST_SEQNUM;
    nextSeqNum = FIRST_SEQNUM;

    // Statistics
    txPktCount = 0;
    rxPktCount = 0;
    corruptPktCount = 0;
    rxmtCount = 0;
    txPktTime = (double *) malloc(sizeof(double) * LIMIT_SEQNUM);
    totalTtc = 0;
    commPktCount = 0;
    receivedMsgCount = 0;
    msgArriveTime = (double *) malloc(sizeof(double) * nsimmax);
    totalDelay = 0;
} 

// Called from layer 3, when a packet arrives for layer 4 at B
void B_input(pkt packet)
{
    printf("  B: Receiving DATA from A...\n");
    printf("    SEQ, ACK: %d, %d\n", packet.seqnum, packet.acknum);
    printf("    CHECKSUM: %d\n", packet.checksum);
    printf("    PAYLOAD: %.*s\n", 19, packet.payload);

    pkt new_packet;

    // Update packet count
    rxPktCount++;

    // Packet not corrupted and SEQ number is new
    if (!isCorrupt(packet) && packet.seqnum == expectSeqNum)
    {
        // Send message to above
        toLayer5(packet.payload);

        // Update received message count
        receivedMsgCount++;

        // Create ACK packet
        new_packet.seqnum = 0;
        new_packet.acknum = packet.seqnum;
        memcpy(new_packet.payload, packet.payload, sizeof(packet.payload));
        new_packet.checksum = calcChecksum(new_packet);

        // Send packet to network
        printf("  B: Sending new ACK to A...\n");
        printf("    SEQ, ACK: %d, %d\n", new_packet.seqnum, new_packet.acknum);
        printf("    CHECKSUM: %d\n", new_packet.checksum);
        printf("    PAYLOAD: %.*s\n", 19, new_packet.payload);
        toLayer3(B, new_packet);

        // Record ACK number
        lastAckNum = new_packet.acknum;

        // Update expected sequence number
        expectSeqNum = (expectSeqNum + 1) % LIMIT_SEQNUM;
    }

    // Packet is corrupted or has invalid SEQ number
    else
    {
        // Update corrupted packet count
        if (isCorrupt(packet))
            corruptPktCount++;

        // Create ACK packet for previously acknowledged DATA packet
        new_packet.seqnum = 0;
        new_packet.acknum = lastAckNum;
        memcpy(new_packet.payload, packet.payload, sizeof(packet.payload));
        new_packet.checksum = calcChecksum(new_packet);

        // Send packet to network
        printf("  B: Resending previous ACK to A...\n");
        printf("    SEQ, ACK: %d, %d\n", new_packet.seqnum, new_packet.acknum);
        printf("    CHECKSUM: %d\n", new_packet.checksum);
        printf("    PAYLOAD: %.*s\n", 19, new_packet.payload);
        toLayer3(B, new_packet);
    }
}

// Called once before any other entity B routines are called
void B_init(void)
{
    // State variables
    expectSeqNum = FIRST_SEQNUM;
    lastAckNum = FIRST_SEQNUM - 1 < 0 ? LIMIT_SEQNUM - 1 : FIRST_SEQNUM - 1;
} 

// Called at end of simulation to print final statistics
void Simulation_done()
{
    float lossRate, corruptRate, avgRtt, avgTtc, throughput, goodput, avgDelay;
    int i;

    /*
    printf("\nRemaining messages in buffer:\n");
    for (i = nextMsg; i < msgCount; i++)
        printf("  %.*s\n", 19, msgBuffer[i].data);

    printf("\nRemaining packets in buffer:\n");
    i = txBase;
    while (i != nextSeqNum) {
        printf("  %.*s\n", 19, txPktBuffer[i].payload);
        i = (i + 1) % LIMIT_SEQNUM;
    }
    */

    lossRate = (double) (txPktCount - rxPktCount) / txPktCount;
    corruptRate = (double) corruptPktCount / rxPktCount;

    printf("\nLoss rate:       %f", lossRate);
    printf("\nCorruption rate: %f", corruptRate);
    printf("\n");

    printf("\nTotal:\n");
    printf("  Messages arrived to A:  %d\n", msgCount);
    printf("  Packets sent from A:    %d\n", txPktCount);
    printf("  Packets retransmitted:  %d\n", rxmtCount);
    printf("  Time to communicate:    %f\n", totalTtc);
    printf("  Packets communicated:   %d\n", commPktCount);
    printf("  Packets received by B:  %d\n", rxPktCount);
    printf("  Messages received by B: %d\n", receivedMsgCount);
    printf("  Total queuing delay:    %f\n", totalDelay);

    avgTtc = totalTtc / commPktCount;
    throughput = rxPktCount / time_now;
    goodput = receivedMsgCount / time_now;
    avgDelay = totalDelay / msgCount;

    printf("\nAverage:\n");
    printf("  Time to communicate:   %f\n", avgTtc);
    printf("  Throughput (packets):  %f\n", throughput);
    printf("  Goodput (messages):    %f\n", goodput);
    printf("  Queuing delay:         %f\n", avgDelay);

    /*
    // Print to statistics file
    FILE *file;
    file = fopen("gbn.csv", "a");
    fprintf(file, "%f, %f, %d, %f, %f\n",
            lossProb,
            corruptProb,
            rxmtCount,
            avgTtc,
            throughput,
            goodput,
            avgDelay
            );
    fclose(file);
    */

    printf("\n");
}

/*****************************************************************
***************** NETWORK EMULATION CODE STARTS BELOW ***********
The code below emulates the layer 3 and below network environment:
  - emulates the tranmission and delivery (possibly with bit-level corruption
    and packet loss) of packets across the layer 3/4 interface
  - handles the starting/stopping of a timer, and generates timer
    interrupts (resulting in calling students timer handler).
  - generates message to be sent (passed from later 5 to 4)

THERE IS NO REASON THAT ANY STUDENT SHOULD HAVE TO READ OR UNDERSTAND
THE CODE BELOW.  YOU SHOLD NOT TOUCH, OR REFERENCE (in your code) ANY
OF THE DATA STRUCTURES BELOW.  If you're interested in how I designed
the emulator, you're welcome to look at the code - but again, you should have
to, and you defeinitely should not have to modify
******************************************************************/

struct event {
    double evtime;          /* event time */
    int evtype;             /* event type code */
    int eventity;           /* entity where event occurs */
    struct pkt *pktptr;     /* ptr to packet (if any) assoc w/ this event */
    struct event *prev;
    struct event *next;
};

struct event *evlist = NULL;    /* the event list */

/* Advance declarations. */
void init(void);
void generate_next_arrival(void);
void insertevent(struct event *p);

/* possible events: */
#define  TIMER_INTERRUPT 0
#define  FROM_LAYER5     1
#define  FROM_LAYER3     2

#define  OFF             0
#define  ON              1

int TRACE = 0;              /* for debugging purpose */
int fileoutput; 
double time_now = 0.000;
int WINDOW_SIZE;
int LIMIT_SEQNUM;
double RXMT_TIMEOUT;
double lossProb;            /* probability that a packet is dropped  */
double corruptProb;         /* probability that one bit is packet is flipped */
double lambda;              /* arrival rate of messages from layer 5 */
int ntolayer3;              /* number sent into layer 3 */
int nlost;                  /* number lost in medium */
int ncorrupt;               /* number corrupted by medium */
int nsim = 0;
int nsimmax = 0;
unsigned int seed[5];       /* seed used in the pseudo-random generator */

int main(int argc, char **argv)
{
    struct event *eventptr;
    struct msg msg2give;
    struct pkt pkt2give;

    int i, j;

    init();
    A_init();
    B_init();

    while (1)
    {
        eventptr = evlist;            /* get next event to simulate */
        if (eventptr == NULL)
            goto terminate;

        evlist = evlist->next;        /* remove this event from event list */
        if (evlist != NULL)
            evlist->prev = NULL;

        if (TRACE >= 2)
        {
            printf("\nEVENT time: %f,", eventptr->evtime);
            printf("  type: %d", eventptr->evtype);

            if (eventptr->evtype==0)
                printf(", timerinterrupt  ");
            else if (eventptr->evtype==1)
                printf(", fromlayer5 ");
            else
                printf(", fromlayer3 ");

            printf(" entity: %d\n", eventptr->eventity);
        }

        time_now = eventptr->evtime;    /* update time to next event time */
        if (eventptr->evtype == FROM_LAYER5)
        {
            generate_next_arrival();    /* set up future arrival */
            /* fill in msg to give with string of same letter */

    	    j = nsim % 26;

    	    for (i = 0; i < 20; i++)
    	        msg2give.data[i] = 97 + j;

    	    msg2give.data[19] = '\n';
    	    nsim++;

    	    if (nsim == nsimmax + 1)
    	        break;

    	    A_output(msg2give);
	    }
        else if (eventptr->evtype == FROM_LAYER3)
        {
            pkt2give.seqnum = eventptr->pktptr->seqnum;
            pkt2give.acknum = eventptr->pktptr->acknum;
            pkt2give.checksum = eventptr->pktptr->checksum;

	        for (i = 0; i < 20; i++)
	            pkt2give.payload[i] = eventptr->pktptr->payload[i];

            if (eventptr->eventity == A)    /* deliver packet by calling */
                A_input(pkt2give);          /* appropriate entity */
            else
                B_input(pkt2give);

            free(eventptr->pktptr);         /* free the memory for packet */
        }
        else if (eventptr->evtype == TIMER_INTERRUPT)
        {
	        A_timerInterrupt();
        }
        else
        {
            printf("INTERNAL PANIC: unknown event type \n");
        }

        free(eventptr);
    }

terminate:

    Simulation_done(); /* allow students to output statistics */
    printf("Simulator terminated at time %.12f\n", time_now);

    return (0);
}

/* Initialize the simulator */
void init(void)
{
    int i = 0;

    printf("----- * Go-Back-N Network Simulator Version 1.1 * ------ \n\n");

    printf("Enter number of messages to simulate: ");
    scanf("%d", &nsimmax);

    printf("Enter packet loss probability [enter 0.0 for no loss]: ");
    scanf("%lf", &lossProb);

    printf("Enter packet corruption probability [0.0 for no corruption]: ");
    scanf("%lf", &corruptProb);

    printf("Enter average time between messages from sender's layer5 [ > 0.0]: ");
    scanf("%lf", &lambda);

    printf("Enter window size [>0]: ");
    scanf("%d", &WINDOW_SIZE);
    LIMIT_SEQNUM = WINDOW_SIZE + 1;

    printf("Enter retransmission timeout [> 0.0]: ");
    scanf("%lf", &RXMT_TIMEOUT);

    printf("Enter trace level: ");
    scanf("%d", &TRACE);

    printf("Enter random seed: [>0]: ");
    scanf("%d", &seed[0]);

    for (i = 1; i < 5; i++)
        seed[i] = seed[0] + i;

    fileoutput = open("OutputFile", O_CREAT|O_WRONLY|O_TRUNC, 0644);

    if (fileoutput < 0) 
        exit(1);

    ntolayer3 = 0;
    nlost = 0;
    ncorrupt = 0;
    time_now = 0.0;             /* initialize time to 0.0 */

    generate_next_arrival();    /* initialize event list */
}

/****************************************************************************/
/* mrand(): return a double in range [0,1].  The routine below is used to */
/* isolate all random number generation in one location.  We assume that the*/
/* system-supplied rand() function return an int in therange [0,mmm]        */
/*     modified by Chong Wang on Oct.21,2005                                */
/****************************************************************************/
int nextrand(int i)
{
    seed[i] = seed[i] * 1103515245 + 12345;

    return (unsigned int) (seed[i] / 65536) % 32768;
}

double mrand(int i)
{
    double mmm = 32767;     /* largest int  - MACHINE DEPENDENT!!!!!!!!   */
    double x;               /* individual students may need to change mmm */

    x = nextrand(i) / mmm;  /* x should be uniform in [0,1] */

    if (TRACE == 0)
        printf("%.16f\n", x);

    return(x);
}

/********************* EVENT HANDLINE ROUTINES *******/
/*  The next set of routines handle the event list   */
/*****************************************************/
void generate_next_arrival(void)
{
    double x, log(), ceil();
    struct event *evptr;
    //char *malloc(); commented out by Matta 10/17/2013

    if (TRACE > 2)
        printf("          GENERATE NEXT ARRIVAL: creating new arrival\n");

    x = lambda * mrand(0) * 2;  /* x is uniform on [0,2*lambda] */
                                /* having mean of lambda        */
    evptr = (struct event *) malloc(sizeof(struct event));
    evptr->evtime = time_now + x;
    evptr->evtype = FROM_LAYER5;
    evptr->eventity = A;

    insertevent(evptr);
}

void insertevent(p)
   struct event *p;
{
    struct event *q, *qold;

    if (TRACE > 2) {
        printf("            INSERTEVENT: time is %f\n", time_now);
        printf("            INSERTEVENT: future time will be %f\n", p->evtime);
    }

    q = evlist;         /* q points to header of list in which p struct inserted */

    if (q == NULL) {    /* list is empty */
        evlist = p;
        p->next = NULL;
        p->prev = NULL;
    } else {
        for (qold = q; q != NULL && p->evtime > q->evtime; q = q->next)
            qold = q;
        if (q == NULL) {            /* end of list */
            qold->next = p;
            p->prev = qold;
            p->next = NULL;
        } else if (q == evlist) {   /* front of list */
            p->next = evlist;
            p->prev = NULL;
            p->next->prev = p;
            evlist = p;
        } else {                    /* middle of list */
            p->next = q;
            p->prev = q->prev;
            q->prev->next = p;
            q->prev = p;
        }
    }
}

void printevlist(void)
{
    struct event *q;

    printf("--------------\nEvent List Follows:\n");

    for(q = evlist; q != NULL; q = q->next)
        printf("Event time: %f, type: %d entity: %d\n", q->evtime, q->evtype, q->eventity);

    printf("--------------\n");
}

/********************** Student-callable ROUTINES ***********************/

/* called by students routine to cancel a previously-started timer */
void stopTimer(AorB)
    int AorB;  /* A or B is trying to stop timer */
{
    struct event *q /* ,*qold */;

    if (TRACE > 2)
        printf("          STOP TIMER: stopping timer at %f\n", time_now);

    /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
    for (q = evlist; q != NULL ; q = q->next)
        if (q->evtype == TIMER_INTERRUPT  && q->eventity == AorB) {
            /* remove this event */
            if (q->next == NULL && q->prev == NULL)
                evlist = NULL;          /* remove first and only event on list */
            else if (q->next == NULL)   /* end of list - there is one in front */
                q->prev->next = NULL;
            else if (q == evlist) {     /* front of list - there must be event after */
                q->next->prev = NULL;
                evlist = q->next;
            } else {                    /* middle of list */
                q->next->prev = q->prev;
                q->prev->next = q->next;
            }

            free(q);

            return;
        }

    printf("Warning: unable to cancel your timer. It wasn't running.\n");
}

void startTimer(AorB, increment)
    int AorB;  /* A or B is trying to stop timer */
    double increment;
{
    struct event *q;
    struct event *evptr;
    // char *malloc(); commented out by matta 10/17/2013

    if (TRACE > 2)
        printf("          START TIMER: starting timer at %f\n", time_now);

    /* be nice: check to see if timer is already started, if so, then  warn */
    /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
    for (q = evlist; q != NULL ; q = q->next)
        if ( (q->evtype == TIMER_INTERRUPT  && q->eventity == AorB) ) {
            printf("Warning: attempt to start a timer that is already started\n");
            return;
        }

    /* create future event for when timer goes off */
    evptr = (struct event *) malloc(sizeof(struct event));
    evptr->evtime = time_now + increment;
    evptr->evtype = TIMER_INTERRUPT;
    evptr->eventity = AorB;

    insertevent(evptr);
}

/************************** toLayer3 ***************/
void toLayer3(AorB, packet)
    int AorB;  /* A or B is trying to stop timer */
    struct pkt packet;
{
    struct pkt *mypktptr;
    struct event *evptr, *q;
    //char *malloc(); commented out by Matta 10/17/2013
    double lastime, x;
    int i;

    ntolayer3++;

    /* simulate losses: */
    if (mrand(1) < lossProb)  {
        nlost++;

        if (TRACE > 0)
            printf("          toLayer3: packet being lost\n");

        return;
    }

    /* make a copy of the packet student just gave me since he/she may decide */
    /* to do something with the packet after we return back to him/her */
    mypktptr = (struct pkt *) malloc(sizeof(struct pkt));
    mypktptr->seqnum = packet.seqnum;
    mypktptr->acknum = packet.acknum;
    mypktptr->checksum = packet.checksum;

    for (i = 0; i < 20; i++)
        mypktptr->payload[i] = packet.payload[i];

    if (TRACE > 2)
        printf("          toLayer3: seq: %d, ack %d, check: %d ", mypktptr->seqnum, mypktptr->acknum, mypktptr->checksum);

    /* create future event for arrival of packet at the other side */
    evptr = (struct event *) malloc(sizeof(struct event));
    evptr->evtype = FROM_LAYER3;        /* packet will pop out from layer3 */
    evptr->eventity = (AorB + 1) % 2;   /* event occurs at other entity */
    evptr->pktptr = mypktptr;           /* save ptr to my copy of packet */
    /* finally, compute the arrival time of packet at the other end.
       medium can not reorder, so make sure packet arrives between 1 and 10
       time units after the latest arrival time of packets
       currently in the medium on their way to the destination */
    lastime = time_now;

    /* for (q = evlist; q != NULL && q->next != NULL; q = q->next) */
    for (q = evlist; q!=NULL ; q = q->next)
        if (q->evtype == FROM_LAYER3 && q->eventity == evptr->eventity)
            lastime = q->evtime;

    evptr->evtime = lastime + 1 + 9 * mrand(2);

    /* simulate corruption: */
    /* modified by Chong Wang on Oct.21, 2005  */
    if (mrand(3) < corruptProb) {
        ncorrupt++;

        if ((x = mrand(4)) < 0.75)
            mypktptr->payload[0] = '?';   /* corrupt payload */
        else if (x < 0.875)
            mypktptr->seqnum = 999999;
        else
            mypktptr->acknum = 999999;

        if (TRACE > 0)
            printf("          TOLAYER3: packet being corrupted\n");
    }

    if (TRACE > 2)
        printf("          TOLAYER3: scheduling arrival on other side\n");

    insertevent(evptr);
}

void toLayer5(datasent)
    char datasent[20];
{
    write(fileoutput, datasent, 20);
}

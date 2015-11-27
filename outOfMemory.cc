/*
 * outOfMemory.cc
 *
 * Originally based on http://hope.nyc.ny.us/~lprimak/java/demo/jvmti/heapViewer/
 *
 * Original source is Copyright (c) 2004 Sun Microsystems, Inc. All Rights Reserved.
 *
 * Modifications are Copyright (c) 2011 The PolarBear Authors. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * -Redistribution of source code must retain the above copyright notice, this
 *  list of conditions and the following disclaimer.
 *
 * -Redistribution in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * Neither the name of Sun Microsystems, Inc. or the names of contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * This software is provided "AS IS," without a warranty of any kind. ALL
 * EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES, INCLUDING
 * ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED. SUN MIDROSYSTEMS, INC. ("SUN")
 * AND ITS LICENSORS SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE
 * AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS
 * DERIVATIVES. IN NO EVENT WILL SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST
 * REVENUE, PROFIT OR DATA, OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL,
 * INCIDENTAL OR PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY
 * OF LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE,
 * EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * You acknowledge that this software is not designed, licensed or intended
 * for use in the design, construction, operation or maintenance of any
 * nuclear facility.
 */
#include <iostream>
#include <signal.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "outOfMemory.h"
#include "base.h"
#include "memory.h"
#include "threads.h"

enum {
    TIME_OPT = 0,
    COUNT_OPT,
    DUMPHEAP_OPT,
    DUMPTHREADS_OPT,
    ANALYZECLASSES_OPT,
    THE_END
};
 
char *tokens[] = {
    [TIME_OPT] = "time",
    [COUNT_OPT] ="count",
    [DUMPHEAP_OPT] = "dumpHeap",
    [DUMPTHREADS_OPT] = "dumpThreads",
    [ANALYZECLASSES_OPT] = "analyzeClasses",
    [THE_END] = NULL
};



static time_t currentTimestamp;
static int count;

static void thresholdReached(jvmtiEnv *jvmti, JNIEnv* jni) {
//    enterAgentMonitor(jvmti); {
      std::cout << "About to throw an OutOfMemory error.\n";

      std::cout << "Printing thread dump.\n";

//      ThreadSuspension threads(jvmti);

      printThreadDump(jvmti, &std::cout);

      std::cout << "Suspending all threads except the current one.\n";

      //threads.suspend();


      std::cout << "Printing a heap histogram.\n";

      printHistogram(jvmti, &(std::cout), true);

      std::cout << "Resuming threads.\n";

      //threads.resume();

      std::cout << "\n\n";
 //   } exitAgentMonitor(jvmti);

    std::cerr << "killing current process\n";
    kill(getpid(), gdata->signal);
}

/* Called when memory is exhausted. */
static void JNICALL resourceExhausted(
    jvmtiEnv *jvmti, JNIEnv* jni, jint flags, const void* reserved, const char* description) {
    std::cerr << "ResourceExhausted!\n";
    time_t evaluated = time(NULL)/gdata->timeThreshold;
    if (currentTimestamp == evaluated) {
        count++;
        if (count > gdata->countThreshold) {
            thresholdReached(jvmti, jni);
        }
    }
    else {
        count=1;
        currentTimestamp = evaluated;
    }
}

void setAnalyzeClasses(char *options) {
  /* Build list of filter classes. */
  if (options && options[0]) {
    int len = strlen(options);
    int separatorCount = 0;
    gdata->optionsCopy = strdup(options);
    for (int i = 0; i < len; i++) {
      if (options[i] == '+') {
        separatorCount += 1;
      }
    }
    gdata->retainedSizeClassCount = separatorCount + 1;
    if (separatorCount == 0) {
      gdata->retainedSizeClasses = &gdata->optionsCopy;
    } else {
      gdata->retainedSizeClasses = (char **) calloc(sizeof(char *), gdata->retainedSizeClassCount);
      char *base = gdata->optionsCopy;
      int j = 0;
      for (int i = 0; i < len; i++) {
        if (gdata->optionsCopy[i] == '+') {
          gdata->optionsCopy[i] = 0;
          gdata->retainedSizeClasses[j] = base;
          base = gdata->optionsCopy + (i + 1);
          j++;
        }
      }
      gdata->retainedSizeClasses[j] = base;
    }
  } else {
    gdata->retainedSizeClassCount = 0;
  }
}


void setParameters(char *options) {
   char *subopts;
   char *value;

   //sets defaults
   gdata->countThreshold = 0;
   gdata->timeThreshold = 1;

   if (NULL == options)
       return;
std::cout << options;
   subopts = strdup(options);
   while (*subopts != '\0')
      switch (getsubopt (&subopts, tokens, &value)) {
std::cout << value;
         case COUNT_OPT:
            if (value == NULL)
               abort ();
            gdata->countThreshold = atoi (value);
            break;
         case TIME_OPT:
            if (value == NULL)
               abort ();
            gdata->timeThreshold = atoi (value);
            break;
         default:
            /* Unknown suboption. */
            std::cerr << "Unknown suboption '" << value  << "\n";
            break;
      }
}


/* Called by the JVM to load the module. */
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
  jint rc;
  jvmtiError err;
  jvmtiCapabilities capabilities;
  jvmtiEventCallbacks callbacks;
  jvmtiEnv *jvmti;

  gdata->signal = SIGKILL;

  std::cout << "Initializing polarbear.\n\n";
  setParameters(options);

  if (gdata->retainedSizeClassCount) {
    std::cout << "Performing retained size analysis for" << gdata->retainedSizeClassCount << " classes:\n";
    for (int i = 0; i < gdata->retainedSizeClassCount; i++) {
      std::cout << gdata->retainedSizeClasses[i] << "\n";
    }
    std::cout << "\n";
  }

  /* Get JVMTI environment */
  jvmti = NULL;
  rc = vm->GetEnv((void **)&jvmti, JVMTI_VERSION);
  if (rc != JNI_OK) {
    std::cerr << "ERROR: Unable to create jvmtiEnv, GetEnv failed, error=" << rc << "\n";
    return JNI_ERR;
  }
  CHECK_FOR_NULL(jvmti);

  /* Get/Add JVMTI capabilities */
  CHECK(jvmti->GetCapabilities(&capabilities));
  capabilities.can_tag_objects = 1;
  capabilities.can_generate_garbage_collection_events = 1;
  capabilities.can_get_source_file_name = 1;
  capabilities.can_get_line_numbers = 1;
  capabilities.can_suspend = 1;
  CHECK(jvmti->AddCapabilities(&capabilities));

  /* Create the raw monitor */
  CHECK(jvmti->CreateRawMonitor("agent lock", &(gdata->lock)));

  /* Set callbacks and enable event notifications */
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.ResourceExhausted = resourceExhausted;
  CHECK(jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)));
  CHECK(jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_RESOURCE_EXHAUSTED, NULL));

  return JNI_OK;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
}

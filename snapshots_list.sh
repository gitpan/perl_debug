#!/usr/bin/sh

# already finished: 20537

for i in \
20512 \
20505 \
20320 \
20277 \
20102 \
20095 \
20073 \
20046 \
20014 \
19997 \
19975 \
19956 \
19923 ;\
do buildaperl -branch=maint-5.8 \@$i ;\
done

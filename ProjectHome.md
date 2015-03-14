Some harddisk drives like some made by "Western Digital" claim to have smart head parking feature (intellipark). These parking by default is very aggressive (head gets parked with 8 seconds of inactivity).

Because of this and the way Linux works (e.g ext3/4 journal-ing), harddisks go through frequent parking and unparking. This causes LOAD\_CYCLE\_COUNT (as reported by programs like smartctl) increase way too rapidly.(150+ within an hour)

Since drive manufacturers claim to have capacity of 300000 to 600000 of load cycles, in such cases harddrive can die within a year.

This program tries to work around this potential problem by causing disk activity (only if necessary) so as NOT to trigger disk parking every 8 seconds but instead after user customizable interval.


Enjoy, the safety of your drive and drive safely!